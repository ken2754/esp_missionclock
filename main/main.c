#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "settings_mgr.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "wifi_portal.h"
#include "sntp_time.h"
#include "terminal_ui.h"
#include "mission_engine.h"

static const char *TAG = "main";

static void on_touch_detected(void)
{
    int tx = 0, ty = 0;
    if (touch_driver_get_xy(&tx, &ty)) {
        ESP_LOGI(TAG, "Touch event at (%d, %d)", tx, ty);
        if (display_driver_get_brightness() == 0) {
            display_driver_cycle_brightness();
            return;
        }
        mission_engine_handle_touch(tx, ty);
    }
}

static void on_wifi_state_changed(wifi_state_t state, const char *info)
{
    ESP_LOGI(TAG, "Wi-Fi State: %d, info: %s", state, info ? info : "none");
    if (state == WIFI_STATE_CONNECTED) {
        sntp_time_restart();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, " Starting ESP32 CYD NASA Apollo Mission Clock");
    ESP_LOGI(TAG, "==========================================================");

    // 1. Initialize NVS and Settings
    ESP_ERROR_CHECK(settings_mgr_init());
    const app_settings_t *settings = settings_mgr_get();

    // 2. Initialize Display (ST7789 / ILI9341 auto-fallback) and LVGL
    ESP_ERROR_CHECK(display_driver_init());
    display_driver_set_backlight(settings->brightness);

    // 3. Initialize Touch Screen (XPT2046)
    ESP_ERROR_CHECK(touch_driver_init(on_touch_detected));

    // 4. Initialize Terminal UI (12-line Retro CRT style)
    terminal_ui_init();

    // 5. Initialize Mission Engine Data
    mission_engine_init();

    // 6. Initialize Wi-Fi & Captive Portal / SNTP
    ESP_ERROR_CHECK(wifi_portal_init(on_wifi_state_changed));

    // 7. Start Mission Engine Background Task
    mission_engine_start();

    ESP_LOGI(TAG, "NASA Apollo Mission Clock started successfully.");

    // Main GUI Loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));

        // Poll touch screen
        touch_driver_poll();

        // Handle LVGL timers and rendering
        terminal_ui_timer_handler();
    }
}
