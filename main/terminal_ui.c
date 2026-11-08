#include "terminal_ui.h"
#include "term_font_8x16.h"
#include "settings_mgr.h"
#include "mission_data.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "esp_log.h"

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

// Circular buffer of recent visible log entries
static terminal_log_entry_t s_entries[LOG_ROWS];
static int s_entry_count = 0;

static void format_time_str(char *buf, size_t buf_len, const terminal_log_entry_t *e, uint8_t time_mode)
{
    if (time_mode == TIME_MODE_LOCAL_OFFSET || e->is_joke) {
        time_t t = (time_t)e->local_sec;
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        snprintf(buf, buf_len, "%02d:%02d:%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
        int32_t s = e->get_sec;
        if (s < 0) {
            int32_t abs_s = -s;
            int h = abs_s / 3600;
            int m = (abs_s % 3600) / 60;
            int sec = abs_s % 60;
            snprintf(buf, buf_len, "T-%02d:%02d:%02d", h, m, sec);
        } else {
            int d = s / 86400;
            int h = (s % 86400) / 3600;
            int m = (s % 3600) / 60;
            int sec = s % 60;
            if (d > 0) {
                snprintf(buf, buf_len, "T+%d:%02d:%02d", d, h, m);
            } else {
                snprintf(buf, buf_len, "T+%02d:%02d:%02d", h, m, sec);
            }
        }
    }
}

static void render_row(int r)
{
    if (r < 0 || r >= LOG_ROWS || !s_log_labels[r]) return;

    if (r >= s_entry_count) {
        lv_label_set_text_static(s_log_labels[r], "");
        return;
    }

    const terminal_log_entry_t *e = &s_entries[r];
    const app_settings_t *s = settings_mgr_get();

    char line_buf[256];

    if (e->is_continuation) {
        // Continuation line: indent to align with message text
        if (e->is_joke) {
            snprintf(line_buf, sizeof(line_buf), "                #ffffff %s#", e->text);
        } else {
            snprintf(line_buf, sizeof(line_buf), "                #00ff41 %s#", e->text);
        }
    } else if (e->speaker_id == SPK_OTHER && e->text[0] == '-') {
        // System banner / silence warp line
        snprintf(line_buf, sizeof(line_buf), "#00aa33 %s#", e->text);
    } else {
        // Standard utterance line
        char time_buf[24];
        format_time_str(time_buf, sizeof(time_buf), e, s->time_mode);
        const char *spk_tag = mission_data_get_speaker_tag(s->mission_id, e->speaker_id);

        if (e->is_joke) {
            // Bright White for jokes & Easter eggs
            snprintf(line_buf, sizeof(line_buf), "#aaaaaa [%s]# #ffffff %-4s# #ffffff %s#",
                     time_buf, spk_tag, e->text);
        } else {
            // CRT Green palette
            snprintf(line_buf, sizeof(line_buf), "#007722 [%s]# #00aa33 %-4s# #00ff41 %s#",
                     time_buf, spk_tag, e->text);
        }
    }

    lv_label_set_text(s_log_labels[r], line_buf);
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

    // Status bar: Clock label (Left)
    s_status_clock_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_clock_lbl, 6, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_clock_lbl, true);
    lv_obj_set_style_text_font(s_status_clock_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_clock_lbl, "#00ff41 00:00:00 JST#");

    // Status bar: Mission & Mode label (Center)
    s_status_mission_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_mission_lbl, 136, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_mission_lbl, true);
    lv_obj_set_style_text_font(s_status_mission_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_mission_lbl, "#00aa33 A17 [LOCAL]#");

    // Status bar: WiFi status label (Right)
    s_status_wifi_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_wifi_lbl, 244, STATUS_BAR_Y + 5);
    lv_label_set_recolor(s_status_wifi_lbl, true);
    lv_obj_set_style_text_font(s_status_wifi_lbl, &lv_font_term_8x16, LV_PART_MAIN);
    lv_label_set_text(s_status_wifi_lbl, "#007722 WIFI:--#");

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);

    ESP_LOGI(TAG, "Terminal UI initialized (12 lines, crisp 8x16 font, 39 chars/line).");
}

static void push_single_row(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke, bool is_continuation)
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
        // Shift rows up
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

void terminal_ui_add_log(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke)
{
    if (!text || strlen(text) == 0) return;
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);

    // If it's a system banner (starts with ---), just push 1 row
    if (text[0] == '-') {
        push_single_row(get_sec, local_sec, speaker_id, text, is_joke, false);
    } else {
        // Word wrap: First line has ~22 chars available, continuation lines have ~22 chars available
        const int first_max = 22;
        const int cont_max  = 22;

        const char *p = text;
        bool is_first = true;

        while (*p) {
            while (*p == ' ') p++; // skip leading spaces
            if (!*p) break;

            int limit = is_first ? first_max : cont_max;
            int len = strlen(p);

            if (len <= limit) {
                // Remainder fits
                push_single_row(get_sec, local_sec, speaker_id, p, is_joke, !is_first);
                break;
            } else {
                // Find last space before limit
                int split = limit;
                while (split > 0 && p[split] != ' ') {
                    split--;
                }
                if (split == 0) {
                    // No space found, hard cut
                    split = limit;
                }

                char chunk[32];
                int copy_bytes = split < sizeof(chunk) - 1 ? split : sizeof(chunk) - 1;
                strncpy(chunk, p, copy_bytes);
                chunk[copy_bytes] = '\0';

                push_single_row(get_sec, local_sec, speaker_id, chunk, is_joke, !is_first);
                p += split;
                is_first = false;
            }
        }
    }

    // Re-render visible rows
    for (int i = 0; i < s_entry_count; i++) {
        render_row(i);
    }

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

void terminal_ui_refresh_all_lines(void)
{
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    for (int i = 0; i < s_entry_count; i++) {
        render_row(i);
    }
    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

void terminal_ui_update_status(const char *clock_str, const char *mission_str, const char *mode_str, const char *wifi_str)
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

    snprintf(buf, sizeof(buf), "#007722 %s#", wifi_str ? wifi_str : "WIFI:--");
    lv_label_set_text(s_status_wifi_lbl, buf);

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}

void terminal_ui_timer_handler(void)
{
    if (s_lvgl_mux && xSemaphoreTake(s_lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
        lv_timer_handler();
        xSemaphoreGive(s_lvgl_mux);
    }
}

void terminal_ui_clear(void)
{
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    s_entry_count = 0;
    for (int i = 0; i < LOG_ROWS; i++) {
        if (s_log_labels[i]) lv_label_set_text_static(s_log_labels[i], "");
    }
    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);
}
