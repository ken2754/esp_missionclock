#include "terminal_ui.h"
#include "term_font_8x16.h"
#include "settings_mgr.h"
#include "mission_data.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "terminal_ui";

#define LOG_ROWS            TERM_LOG_ROWS   // 11 rows
#define LINE_Y_STEP         19              // 16px font + 3px line gap
#define STATUS_BAR_Y        211             // Y offset for status bar
#define STATUS_BAR_HEIGHT   29
#define MAX_CHARS_PER_LINE  39              // 312px / 8px = 39 characters

static SemaphoreHandle_t s_lvgl_mux = NULL;
static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_log_labels[LOG_ROWS];
static lv_obj_t *s_divider = NULL;
static lv_obj_t *s_status_clock_lbl = NULL;
static lv_obj_t *s_status_mission_lbl = NULL;
static lv_obj_t *s_status_wifi_lbl = NULL;

// Circular buffer of visible rows
static terminal_log_entry_t s_entries[LOG_ROWS];
static int s_entry_count = 0;

// Typing animation queue (Enlarged to 64 to never drop words)
#define TYPE_QUEUE_SIZE 64
typedef struct {
    int32_t get_sec;
    int64_t local_sec;
    uint8_t speaker_id;
    bool is_joke;
    bool is_continuation;
    char text[48];
} type_chunk_t;

static type_chunk_t s_type_queue[TYPE_QUEUE_SIZE];
static int s_type_q_head = 0;
static int s_type_q_tail = 0;

// Active typing animation state
static bool s_is_typing = false;
static int  s_typing_row = -1;
static int  s_typing_pos = 0;
static char s_typing_full_target[48];
static int64_t s_typing_next_ms = 0;

// Solid Block Cursor Blink state
// Character '\x7f' is defined in term_font_8x16 as a solid 8x16 filled rectangle
#define CURSOR_BLOCK_CHAR "\x7f"
static bool s_cursor_blink_on = true;
static int64_t s_cursor_next_blink_ms = 0;

// Compact timestamp format: NO brackets, NO colons!
static void format_time_compact(char *buf, size_t buf_len, const terminal_log_entry_t *e, uint8_t time_mode)
{
    if (time_mode == TIME_MODE_LOCAL_OFFSET || e->is_joke) {
        // e.g. "153014" (6 chars)
        time_t t = (time_t)e->local_sec;
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        snprintf(buf, buf_len, "%02d%02d%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
        int32_t s = e->get_sec;
        if (s < 0) {
            // Pre-launch e.g. "-011540" (7 chars)
            int32_t abs_s = -s;
            int h = abs_s / 3600;
            int m = (abs_s % 3600) / 60;
            int sec = abs_s % 60;
            snprintf(buf, buf_len, "-%02d%02d%02d", h, m, sec);
        } else {
            // Post-launch e.g. "+000123" (7 chars) or "+1D0234" (7 chars)
            int d = s / 86400;
            int h = (s % 86400) / 3600;
            int m = (s % 3600) / 60;
            int sec = s % 60;
            if (d > 0) {
                snprintf(buf, buf_len, "+%dD%02d%02d", d, h, m);
            } else {
                snprintf(buf, buf_len, "+%02d%02d%02d", h, m, sec);
            }
        }
    }
}

static void render_row_internal(int r, bool show_cursor)
{
    if (r < 0 || r >= LOG_ROWS || !s_log_labels[r]) return;

    if (r >= s_entry_count) {
        lv_label_set_text_static(s_log_labels[r], "");
        return;
    }

    const terminal_log_entry_t *e = &s_entries[r];
    const app_settings_t *s = settings_mgr_get();

    char line_buf[256];
    char display_text[64];
    strncpy(display_text, e->text, sizeof(display_text) - 4);
    display_text[sizeof(display_text) - 4] = '\0';
    if (show_cursor && s_cursor_blink_on) {
        strcat(display_text, CURSOR_BLOCK_CHAR);
    }

    if (e->speaker_id == SPK_OTHER && e->text[0] == '-') {
        // System banner / silence warp
        snprintf(line_buf, sizeof(line_buf), "#00aa33 %s#", display_text);
    } else if (e->is_continuation) {
        // 2-space indent for continuation line: leaves 37 characters for text!
        if (e->is_joke) {
            snprintf(line_buf, sizeof(line_buf), "  #ffffff %s#", display_text);
        } else {
            snprintf(line_buf, sizeof(line_buf), "  #00ff41 %s#", display_text);
        }
    } else {
        // Main utterance: Space-saving prefix (No brackets, no colons!)
        // LOCAL: "153014 CDR " (11 chars) -> leaves 28 chars for text!
        // HIST:  "-011540 PAO " or "+000123 CDR " (12 chars) -> leaves 27 chars for text!
        char time_buf[16];
        format_time_compact(time_buf, sizeof(time_buf), e, s->time_mode);
        const char *spk_tag = mission_data_get_speaker_tag(s->mission_id, e->speaker_id);

        if (e->is_joke) {
            snprintf(line_buf, sizeof(line_buf), "#aaaaaa %s# #ffffff %-3s# #ffffff %s#",
                     time_buf, spk_tag, display_text);
        } else {
            snprintf(line_buf, sizeof(line_buf), "#007722 %s# #00aa33 %-3s# #00ff41 %s#",
                     time_buf, spk_tag, display_text);
        }
    }

    lv_label_set_text(s_log_labels[r], line_buf);
}

static void push_single_row_direct(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke, bool is_continuation)
{
    if (s_entry_count < LOG_ROWS) {
        terminal_log_entry_t *e = &s_entries[s_entry_count];
        e->get_sec = get_sec;
        e->local_sec = local_sec;
        e->speaker_id = speaker_id;
        e->is_joke = is_joke;
        e->is_continuation = is_continuation;
        strncpy(e->text, text, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
        s_entry_count++;
    } else {
        for (int i = 0; i < LOG_ROWS - 1; i++) {
            s_entries[i] = s_entries[i + 1];
        }
        terminal_log_entry_t *e = &s_entries[LOG_ROWS - 1];
        e->get_sec = get_sec;
        e->local_sec = local_sec;
        e->speaker_id = speaker_id;
        e->is_joke = is_joke;
        e->is_continuation = is_continuation;
        strncpy(e->text, text, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
    }
}

void terminal_ui_init(void)
{
    if (!s_lvgl_mux) {
        s_lvgl_mux = xSemaphoreCreateMutex();
    }
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);

    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, COLOR_CRT_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Create 11 transcript rows with crisp 8x16 font
    for (int i = 0; i < LOG_ROWS; i++) {
        s_log_labels[i] = lv_label_create(s_scr);
        lv_obj_set_pos(s_log_labels[i], 4, 2 + i * LINE_Y_STEP);
        lv_obj_set_size(s_log_labels[i], 312, LINE_Y_STEP);
        lv_label_set_recolor(s_log_labels[i], true);
        lv_obj_set_style_text_font(s_log_labels[i], &lv_font_term_8x16, LV_PART_MAIN);
        lv_label_set_text_static(s_log_labels[i], "");
    }

    // Horizontal divider line above status bar
    s_divider = lv_obj_create(s_scr);
    lv_obj_set_pos(s_divider, 0, STATUS_BAR_Y - 2);
    lv_obj_set_size(s_divider, 320, 2);
    lv_obj_set_style_bg_color(s_divider, COLOR_CRT_DIM_GREEN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_divider, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_divider, LV_OBJ_FLAG_SCROLLABLE);

    // Status bar: Clock label (Left) - e.g. "00:00:00 JST"
    s_status_clock_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_clock_lbl, 4, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_clock_lbl, true);
    lv_obj_set_style_text_font(s_status_clock_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_clock_lbl, "#00ff41 00:00:00 JST#");

    // Status bar: Mission & Mode label (Center) - e.g. "A17 LOC"
    s_status_mission_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_mission_lbl, 116, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_mission_lbl, true);
    lv_obj_set_style_text_font(s_status_mission_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_mission_lbl, "#00aa33 A17 [LOC]#");

    // Status bar: IP Address label (Right) - e.g. "192.168.0.232"
    s_status_wifi_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_wifi_lbl, 196, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_wifi_lbl, true);
    lv_obj_set_style_text_font(s_status_wifi_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_wifi_lbl, "#007722 IP:--#");

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);

    ESP_LOGI(TAG, "Terminal UI initialized with compact timestamps, 2-space continuation indent, and solid block cursor.");
}

static void enqueue_chunk(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke, bool is_continuation)
{
    int next_tail = (s_type_q_tail + 1) % TYPE_QUEUE_SIZE;
    if (next_tail == s_type_q_head) {
        // Queue full: advance head to avoid blocking
        s_type_q_head = (s_type_q_head + 1) % TYPE_QUEUE_SIZE;
    }
    type_chunk_t *item = &s_type_queue[s_type_q_tail];
    item->get_sec = get_sec;
    item->local_sec = local_sec;
    item->speaker_id = speaker_id;
    item->is_joke = is_joke;
    item->is_continuation = is_continuation;
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    s_type_q_tail = next_tail;
}

void terminal_ui_add_log(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke)
{
    if (!text || strlen(text) == 0) return;

    if (text[0] == '-') {
        // System banner / silence warp: enqueue directly
        enqueue_chunk(get_sec, local_sec, speaker_id, text, is_joke, false);
    } else {
        // Line width capacity:
        // Main line: 11-12 chars prefix ("153014 CDR " or "-011540 PAO ") -> 26 chars available for text
        // Continuation line: 2-space indent ("  ") -> 37 - 2 = 35 chars available for text!
        const int first_max = 26;
        const int cont_max  = 35;

        const char *p = text;
        bool is_first = true;

        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            int line_max = is_first ? first_max : cont_max;
            int len = strlen(p);

            if (len <= line_max) {
                enqueue_chunk(get_sec, local_sec, speaker_id, p, is_joke, !is_first);
                break;
            } else {
                int split = line_max;
                while (split > 0 && p[split] != ' ') {
                    split--;
                }
                if (split == 0) split = line_max;

                char chunk[48];
                int copy_bytes = split < sizeof(chunk) - 1 ? split : sizeof(chunk) - 1;
                strncpy(chunk, p, copy_bytes);
                chunk[copy_bytes] = '\0';

                enqueue_chunk(get_sec, local_sec, speaker_id, chunk, is_joke, !is_first);
                p += split;
                is_first = false;
            }
        }
    }
}

// Step typing animation with human cadence, block cursor, and blinking
static void update_typing_animation(int64_t now_ms)
{
    // Update block cursor blink (350ms period)
    if (now_ms >= s_cursor_next_blink_ms) {
        s_cursor_blink_on = !s_cursor_blink_on;
        s_cursor_next_blink_ms = now_ms + 350;
        if (s_is_typing && s_typing_row >= 0) {
            render_row_internal(s_typing_row, true);
        } else if (s_entry_count > 0) {
            render_row_internal(s_entry_count - 1, true);
        } else if (s_log_labels[0]) {
            lv_label_set_text(s_log_labels[0], s_cursor_blink_on ? "#00ff41 \x7f#" : "");
        }
    }

    if (!s_is_typing) {
        if (s_type_q_head != s_type_q_tail) {
            // Dequeue next line
            type_chunk_t item = s_type_queue[s_type_q_head];
            s_type_q_head = (s_type_q_head + 1) % TYPE_QUEUE_SIZE;

            // Push an empty row onto the screen buffer
            push_single_row_direct(item.get_sec, item.local_sec, item.speaker_id, "", item.is_joke, item.is_continuation);
            s_typing_row = s_entry_count - 1;

            strncpy(s_typing_full_target, item.text, sizeof(s_typing_full_target) - 1);
            s_typing_full_target[sizeof(s_typing_full_target) - 1] = '\0';

            s_typing_pos = 0;
            s_is_typing = true;
            s_typing_next_ms = now_ms + 30;

            // Re-render preceding static rows (clears previous cursors)
            for (int i = 0; i < s_typing_row; i++) {
                render_row_internal(i, false);
            }
            // Render current row with initial block cursor
            render_row_internal(s_typing_row, true);
        }
    } else {
        if (now_ms >= s_typing_next_ms) {
            int target_len = strlen(s_typing_full_target);
            if (s_typing_pos < target_len) {
                // Type next character
                char next_ch = s_typing_full_target[s_typing_pos];
                s_typing_pos++;

                s_entries[s_typing_row].text[s_typing_pos - 1] = next_ch;
                s_entries[s_typing_row].text[s_typing_pos] = '\0';

                // Render with moving block cursor
                render_row_internal(s_typing_row, true);

                // Queue catch-up speed boost: if queue has lines pending, speed up typing!
                int pending = (s_type_q_tail - s_type_q_head + TYPE_QUEUE_SIZE) % TYPE_QUEUE_SIZE;
                int speed_factor = (pending > 2) ? 2 : 1;

                // Human-like typing delay with natural hesitation
                int delay_ms;
                if (next_ch == '.' || next_ch == '!' || next_ch == '?') {
                    delay_ms = (140 + (rand() % 60)) / speed_factor;
                } else if (next_ch == ',' || next_ch == ';') {
                    delay_ms = (80 + (rand() % 40)) / speed_factor;
                } else if (next_ch == ' ') {
                    delay_ms = (40 + (rand() % 30)) / speed_factor;
                } else {
                    delay_ms = (20 + (rand() % 25)) / speed_factor;
                }
                s_typing_next_ms = now_ms + delay_ms;
            } else {
                // Done typing this line: keep cursor blinking at end of line
                render_row_internal(s_typing_row, true);
                s_is_typing = false;
                s_typing_next_ms = now_ms + 40; // Brief pause before next line
            }
        }
    }
}

void terminal_ui_refresh_all_lines(void)
{
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    for (int i = 0; i < s_entry_count; i++) {
        bool show_cur = (s_is_typing && i == s_typing_row) || (!s_is_typing && i == s_entry_count - 1);
        render_row_internal(i, show_cur);
    }
    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

void terminal_ui_update_status(const char *clock_str, const char *mission_str, const char *mode_str, const char *wifi_or_ip_str)
{
    if (!s_status_clock_lbl) return;
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);

    char buf[64];
    snprintf(buf, sizeof(buf), "#00ff41 %s#", clock_str ? clock_str : "--:--:--");
    lv_label_set_text(s_status_clock_lbl, buf);

    snprintf(buf, sizeof(buf), "#00aa33 %s [%s]#",
             mission_str ? mission_str : "---",
             mode_str ? mode_str : "LOC");
    lv_label_set_text(s_status_mission_lbl, buf);

    // Displays the actual IP Address (e.g. "192.168.0.232")
    snprintf(buf, sizeof(buf), "#00aa33 %s#", wifi_or_ip_str ? wifi_or_ip_str : "--");
    lv_label_set_text(s_status_wifi_lbl, buf);

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

void terminal_ui_timer_handler(void)
{
    if (s_lvgl_mux && xSemaphoreTake(s_lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        update_typing_animation(now_ms);
        lv_timer_handler();
        xSemaphoreGive(s_lvgl_mux);
    }
}

void terminal_ui_clear(void)
{
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    s_entry_count = 0;
    s_type_q_head = 0;
    s_type_q_tail = 0;
    s_is_typing = false;
    s_typing_row = -1;
    for (int i = 0; i < LOG_ROWS; i++) {
        if (s_log_labels[i]) lv_label_set_text_static(s_log_labels[i], "");
    }
    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

int terminal_ui_get_queue_count(void)
{
    return (s_type_q_tail - s_type_q_head + TYPE_QUEUE_SIZE) % TYPE_QUEUE_SIZE;
}

bool terminal_ui_is_typing(void)
{
    return s_is_typing || (s_type_q_head != s_type_q_tail);
}
