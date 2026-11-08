#include "terminal_ui.h"
#include "settings_mgr.h"
#include "mission_data.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "terminal_ui";

#define LOG_ROWS            TERM_LOG_ROWS   // 11 rows
#define LINE_Y_STEP         19
#define STATUS_BAR_Y        211
#define STATUS_BAR_HEIGHT   29

static SemaphoreHandle_t s_lvgl_mux = NULL;
static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_log_labels[LOG_ROWS];
static lv_obj_t *s_divider = NULL;
static lv_obj_t *s_status_clock_lbl = NULL;
static lv_obj_t *s_status_mission_lbl = NULL;
static lv_obj_t *s_status_wifi_lbl = NULL;

// Circular buffer / shift buffer of recent log entries
static terminal_log_entry_t s_entries[LOG_ROWS];
static int s_entry_count = 0;

static void format_time_str(char *buf, size_t buf_len, const terminal_log_entry_t *entry, uint8_t time_mode)
{
    if (time_mode == TIME_MODE_LOCAL_OFFSET || entry->is_joke) {
        // Format as Local HH:MM:SS
        time_t t = (time_t)entry->local_sec;
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        snprintf(buf, buf_len, "%02d:%02d:%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
        // Format as Historical GET: [T-HH:MM:SS] or [T+DDD:HH:MM:SS]
        int32_t s = entry->get_sec;
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
                snprintf(buf, buf_len, "T+%d:%02d:%02d:%02d", d, h, m, sec);
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

    char time_buf[24];
    format_time_str(time_buf, sizeof(time_buf), e, s->time_mode);

    const char *spk_tag = mission_data_get_speaker_tag(s->mission_id, e->speaker_id);

    char line_buf[256];
    if (e->is_joke) {
        // Hourly Joke Easter Egg: Render in Bright White (#ffffff)
        snprintf(line_buf, sizeof(line_buf), "#aaaaaa [%s]# #ffffff %s:# #ffffff %s#",
                 time_buf, spk_tag, e->text);
    } else {
        // Historical Apollo Log: Render in CRT Phosphor Green palette
        // Timestamp: Dim Green (#007722), Speaker: Medium Green (#00aa33), Text: Bright Green (#00ff41)
        snprintf(line_buf, sizeof(line_buf), "#007722 [%s]# #00aa33 %s:# #00ff41 %s#",
                 time_buf, spk_tag, e->text);
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

    // Create 11 transcript rows
    for (int i = 0; i < LOG_ROWS; i++) {
        s_log_labels[i] = lv_label_create(s_scr);
        lv_obj_set_pos(s_log_labels[i], 4, 2 + i * LINE_Y_STEP);
        lv_obj_set_size(s_log_labels[i], 312, LINE_Y_STEP);
        lv_label_set_recolor(s_log_labels[i], true);
        lv_obj_set_style_text_font(s_log_labels[i], &lv_font_unscii_16, LV_PART_MAIN);
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
    lv_obj_set_pos(s_status_clock_lbl, 6, STATUS_BAR_Y + 4);
    lv_label_set_recolor(s_status_clock_lbl, true);
    lv_obj_set_style_text_font(s_status_clock_lbl, &lv_font_unscii_16, LV_PART_MAIN);
    lv_label_set_text(s_status_clock_lbl, "#00ff41 00:00:00 JST#");

    // Status bar: Mission & Mode label (Center)
    s_status_mission_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_mission_lbl, 136, STATUS_BAR_Y + 4);
    lv_label_set_recolor(s_status_mission_lbl, true);
    lv_obj_set_style_text_font(s_status_mission_lbl, &lv_font_unscii_16, LV_PART_MAIN);
    lv_label_set_text(s_status_mission_lbl, "#00aa33 A17 [LOCAL]#");

    // Status bar: WiFi status label (Right)
    s_status_wifi_lbl = lv_label_create(s_scr);
    lv_obj_set_pos(s_status_wifi_lbl, 244, STATUS_BAR_Y + 4);
    lv_label_set_recolor(s_status_wifi_lbl, true);
    lv_obj_set_style_text_font(s_status_wifi_lbl, &lv_font_unscii_16, LV_PART_MAIN);
    lv_label_set_text(s_status_wifi_lbl, "#007722 WIFI:--#");

    if (s_lvgl_mux) xSemaphoreGive(s_lvgl_mux);

    ESP_LOGI(TAG, "Terminal UI initialized (12 lines, unscii-16, CRT Green).");
}

void terminal_ui_add_log(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke)
{
    if (s_lvgl_mux) xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);

    if (s_entry_count < LOG_ROWS) {
        // Still filling screen
        terminal_log_entry_t *e = &s_entries[s_entry_count];
        e->get_sec = get_sec;
        e->local_sec = local_sec;
        e->speaker_id = speaker_id;
        e->is_joke = is_joke;
        strncpy(e->text, text, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
        s_entry_count++;
    } else {
        // Shift rows up by 1
        for (int i = 0; i < LOG_ROWS - 1; i++) {
            s_entries[i] = s_entries[i + 1];
        }
        terminal_log_entry_t *e = &s_entries[LOG_ROWS - 1];
        e->get_sec = get_sec;
        e->local_sec = local_sec;
        e->speaker_id = speaker_id;
        e->is_joke = is_joke;
        strncpy(e->text, text, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
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

    // Clock
    snprintf(buf, sizeof(buf), "#00ff41 %s#", clock_str ? clock_str : "--:--:--");
    lv_label_set_text(s_status_clock_lbl, buf);

    // Mission + Mode
    snprintf(buf, sizeof(buf), "#00aa33 %s [%s]#",
             mission_str ? mission_str : "---",
             mode_str ? mode_str : "LOC");
    lv_label_set_text(s_status_mission_lbl, buf);

    // WiFi
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
