#include "mission_engine.h"
#include "mission_data.h"
#include "terminal_ui.h"
#include "settings_mgr.h"
#include "sntp_time.h"
#include "wifi_portal.h"
#include "display_driver.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "mission_engine";

static mission_id_t s_curr_mission = MISSION_APOLLO_17;
static uint32_t s_curr_event_idx = 0;
static int64_t s_base_local_sec = 0; // Local time corresponding to GET = 0
static bool s_engine_running = false;
static bool s_sntp_synced_once = false;

// Hourly Joke state
static int s_last_joke_hour = -1;
static bool s_joke_in_progress = false;
static int s_joke_step = 0;
static int s_joke_type = 0;
static int64_t s_joke_step_due_sec = 0;

static const char *const HOUR_WORDS[24] = {
    "twelve o'clock midnight", "one o'clock", "two o'clock", "three o'clock",
    "four o'clock", "five o'clock", "six o'clock", "seven o'clock",
    "eight o'clock", "nine o'clock", "ten o'clock", "eleven o'clock",
    "twelve o'clock noon", "one o'clock", "two o'clock", "three o'clock",
    "four o'clock", "five o'clock", "six o'clock", "seven o'clock",
    "eight o'clock", "nine o'clock", "ten o'clock", "eleven o'clock"
};

static void reset_timeline(mission_id_t mission_id)
{
    s_curr_mission = mission_id;
    s_curr_event_idx = 0;

    time_t now = sntp_time_get_epoch();
    if (now < 1700000000) {
        now = 1700000000; // Fallback sensible epoch if NTP not yet synced
    }

    // Get the first event's GET
    mission_event_t first_ev;
    if (mission_data_get_event(s_curr_mission, 0, &first_ev)) {
        // We set s_base_local_sec such that the first event triggers NOW
        // T_event_local = s_base_local_sec + first_ev.get_sec = now
        // => s_base_local_sec = now - first_ev.get_sec
        s_base_local_sec = (int64_t)now - (int64_t)first_ev.get_sec;
    } else {
        s_base_local_sec = (int64_t)now;
    }

    terminal_ui_clear();

    const mission_info_t *info = mission_data_get_info(s_curr_mission);
    char banner[128];
    snprintf(banner, sizeof(banner), "--- MISSION INITIALIZED: %s (%s) ---", info->full_name, info->subtitle);
    terminal_ui_add_log(first_ev.get_sec, now, SPK_OTHER, banner, false);

    ESP_LOGI(TAG, "Timeline reset for mission %d (%s). Base local sec: %lld (Now: %lld, First GET: %ld)",
             s_curr_mission, info->full_name, (long long)s_base_local_sec, (long long)now, (long)first_ev.get_sec);
}

void mission_engine_on_time_synced(void)
{
    time_t now = sntp_time_get_epoch();
    ESP_LOGI(TAG, "SNTP time synced notification: %lld", (long long)now);

    if (!s_sntp_synced_once) {
        s_sntp_synced_once = true;
        reset_timeline(s_curr_mission);
    }
}

void mission_engine_init(void)
{
    mission_data_init();
    s_curr_mission = g_settings.mission_id;
}

void mission_engine_set_mission(mission_id_t id)
{
    if (id >= MISSION_COUNT) return;
    settings_mgr_set_mission(id);
    reset_timeline(id);
}

void mission_engine_handle_touch(int screen_x, int screen_y)
{
    // Tap anywhere to toggle time mode (Local Offset vs Historical GET)
    ESP_LOGI(TAG, "Screen tap at (%d, %d): Toggling time display mode.", screen_x, screen_y);
    settings_mgr_toggle_time_mode();
    terminal_ui_refresh_all_lines();
}

static void trigger_hourly_joke_step(time_t now, int hour)
{
    const mission_info_t *info = mission_data_get_info(s_curr_mission);
    const char *hour_word = (hour >= 0 && hour < 24) ? HOUR_WORDS[hour] : "four o'clock";

    char text[160];
    uint8_t spk = SPK_CDR;

    // Pick a crew speaker based on joke step and type
    if (s_joke_step == 0) {
        // Astronaut asks the time (+ humorous quip)
        switch (s_joke_type) {
            case 0: // Classic time check
                snprintf(text, sizeof(text), "Houston, do you have the time down there?");
                spk = SPK_CDR;
                break;
            case 1: // Pizza delivery
                snprintf(text, sizeof(text), "Houston, do you have the time? And does the Cocoa Beach pizza place deliver out here?");
                spk = SPK_CMP;
                break;
            case 2: // Siri / AGC accident
                snprintf(text, sizeof(text), "Hey Siri, what time is it down on Earth?");
                spk = SPK_LMP;
                break;
            case 3: // Blue police telephone box
                snprintf(text, sizeof(text), "Houston, mark the time. Also there's a blue police box tumbling outside our window.");
                spk = SPK_CDR;
                break;
            case 4: // WiFi password
                snprintf(text, sizeof(text), "Houston, do you have the time? And what was the Command Module WiFi password again?");
                spk = SPK_CMP;
                break;
            default: // Weather on the Moon
                snprintf(text, sizeof(text), "Houston, what time is it? And how's the weather report for the Moon today?");
                spk = SPK_LMP;
                break;
        }
        terminal_ui_add_log(0, now, spk, text, true);
        s_joke_step = 1;
        s_joke_step_due_sec = now + 9; // 9 seconds later CAPCOM replies
    } else if (s_joke_step == 1) {
        // Houston (CAPCOM) replies with the exact current hour in words
        spk = SPK_CC;
        switch (s_joke_type) {
            case 0:
                snprintf(text, sizeof(text), "%s, Houston. It's %s.", info->id_str, hour_word);
                break;
            case 1:
                snprintf(text, sizeof(text), "%s, it's %s. And negative on pizza, delivery range stops at low Earth orbit.", info->id_str, hour_word);
                break;
            case 2:
                snprintf(text, sizeof(text), "%s, this is Houston. It's %s. And who or what is Siri?", info->id_str, hour_word);
                break;
            case 3:
                snprintf(text, sizeof(text), "%s, mark %s. Flight says do not open the hatch, especially if you hear wheezing.", info->id_str, hour_word);
                break;
            case 4:
                snprintf(text, sizeof(text), "It's %s. WiFi password is 'apollo1969', all lowercase.", hour_word);
                break;
            default:
                snprintf(text, sizeof(text), "It's %s. Forecast is 100%% sunshine, high of 260 degrees, zero rain.", hour_word);
                break;
        }
        terminal_ui_add_log(0, now, spk, text, true);
        s_joke_step = 2;
        s_joke_step_due_sec = now + 8; // 8 seconds later astronaut acknowledges
    } else if (s_joke_step == 2) {
        // Astronaut replies Roger / Copy
        switch (s_joke_type) {
            case 0:
                snprintf(text, sizeof(text), "Roger, Houston. Copy %s.", hour_word);
                spk = SPK_CDR;
                break;
            case 1:
                snprintf(text, sizeof(text), "Copy that, Houston. Back to rehydrated bacon cubes.");
                spk = SPK_CMP;
                break;
            case 2:
                snprintf(text, sizeof(text), "Disregard, Houston. Raytheon must have wired something funny into the DSKY.");
                spk = SPK_LMP;
                break;
            case 3:
                snprintf(text, sizeof(text), "Roger, %s. Keeping the hatch dogged tight.", hour_word);
                spk = SPK_CDR;
                break;
            case 4:
                snprintf(text, sizeof(text), "Roger, %s. Connection reacquired, Houston.", hour_word);
                spk = SPK_CMP;
                break;
            default:
                snprintf(text, sizeof(text), "Roger, %s. Glad we brought the gold-coated visors.", hour_word);
                spk = SPK_LMP;
                break;
        }
        terminal_ui_add_log(0, now, spk, text, true);
        s_joke_step = 3; // Done
        s_joke_in_progress = false;
    }
}

static void mission_engine_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Mission Engine Task started. Waiting for initial time sync...");

    // Wait up to 5 seconds for Wi-Fi and SNTP sync
    int wait_ticks = 0;
    while (!sntp_time_is_synced() && wait_ticks < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ticks++;

        time_t now = sntp_time_get_epoch();
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char clock_buf[32];
        strftime(clock_buf, sizeof(clock_buf), "%H:%M:%S", &tm_info);

        const mission_info_t *info = mission_data_get_info(s_curr_mission);
        const char *ip_str = wifi_portal_is_connected() ? wifi_portal_get_ip() :
                             (wifi_portal_is_ap_active() ? "192.168.4.1" : "--.--.--.--");
        terminal_ui_update_status(clock_buf, info->id_str, "LOC", ip_str);
    }

    s_sntp_synced_once = sntp_time_is_synced();
    reset_timeline(g_settings.mission_id);

    while (1) {
        time_t now = sntp_time_get_epoch();
        struct tm tm_info;
        localtime_r(&now, &tm_info);

        // Update status bar every second
        char clock_buf[32];
        strftime(clock_buf, sizeof(clock_buf), "%H:%M:%S", &tm_info);

        const mission_info_t *info = mission_data_get_info(s_curr_mission);
        const char *mode_str = (g_settings.time_mode == TIME_MODE_LOCAL_OFFSET) ? "LOC" : "GET";
        const char *ip_str = wifi_portal_is_connected() ? wifi_portal_get_ip() :
                             (wifi_portal_is_ap_active() ? "192.168.4.1" : "--.--.--.--");

        terminal_ui_update_status(clock_buf, info->id_str, mode_str, ip_str);

        // 1. Check Hourly Joke Trigger (:00:00 to :00:55)
        if (g_settings.hourly_joke_enabled && tm_info.tm_min == 0 && tm_info.tm_sec < 55) {
            if (s_last_joke_hour != tm_info.tm_hour && !s_joke_in_progress) {
                // Check if any historical event is scheduled within the next 60 seconds
                bool event_in_window = false;
                mission_event_t next_ev;
                if (mission_data_get_event(s_curr_mission, s_curr_event_idx, &next_ev)) {
                    int64_t next_ev_local = s_base_local_sec + next_ev.get_sec;
                    if (next_ev_local >= now && next_ev_local < (now + 60)) {
                        event_in_window = true;
                    }
                }

                if (!event_in_window && tm_info.tm_sec >= 4 && !terminal_ui_is_typing()) {
                    // Start joke easter egg in bright white text
                    s_last_joke_hour = tm_info.tm_hour;
                    s_joke_in_progress = true;
                    s_joke_step = 0;
                    s_joke_type = rand() % 6; // Randomize joke variant
                    ESP_LOGI(TAG, "Triggering hourly joke (Type %d, Hour %d)", s_joke_type, tm_info.tm_hour);
                    trigger_hourly_joke_step(now, tm_info.tm_hour);
                }
            } else if (s_joke_in_progress && now >= s_joke_step_due_sec) {
                trigger_hourly_joke_step(now, tm_info.tm_hour);
            }
        } else {
            if (tm_info.tm_min > 0) {
                s_joke_in_progress = false; // Reset if minute passed
            }
        }

        // 2. Advance Historical Transcript Timeline with Strict Real-Time Cadence
        uint32_t total_events = mission_data_get_count(s_curr_mission);
        while (s_curr_event_idx < total_events) {
            mission_event_t ev;
            if (!mission_data_get_event(s_curr_mission, s_curr_event_idx, &ev)) {
                break;
            }
            int64_t ev_local_sec = s_base_local_sec + ev.get_sec;

            if (now < ev_local_sec) {
                // Historical silence: strictly wait until the authentic historical second arrives!
                // Terminal sits silently with the solid block cursor blinking.
                break;
            }

            // Current time caught up or passed scheduled time (now >= ev_local_sec)
            // Limit feeding if typing queue is busy so we don't drop lines
            if (terminal_ui_get_queue_count() > 3) {
                break;
            }

            // Feed next due utterance to terminal UI
            char clean_text[256];
            int copy_len = ev.text_len < sizeof(clean_text) - 1 ? ev.text_len : sizeof(clean_text) - 1;
            memcpy(clean_text, ev.text, copy_len);
            clean_text[copy_len] = '\0';

            terminal_ui_add_log(ev.get_sec, ev_local_sec, ev.speaker_id, clean_text, false);
            s_curr_event_idx++;
        }

        if (s_curr_event_idx >= total_events) {
            // Mission completed, loop back after a short pause
            vTaskDelay(pdMS_TO_TICKS(15000));
            reset_timeline(s_curr_mission);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Poll every 100ms
    }
}

void mission_engine_start(void)
{
    if (s_engine_running) return;
    s_engine_running = true;
    xTaskCreatePinnedToCore(mission_engine_task, "mission_engine", 4096, NULL, 5, NULL, 1);
}
