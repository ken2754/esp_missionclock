#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[65];
    char timezone[32];              // e.g. "JST-9"
    uint8_t brightness;             // Backlight percent (0, 25, 50, 100)
    uint8_t mission_id;             // mission_id_t (0: A8, 1: A11, 2: A13, 3: A17)
    uint8_t time_mode;              // time_display_mode_t (0: LOCAL_OFFSET, 1: HISTORICAL_GET)
    bool hourly_joke_enabled;       // Enable hourly easter egg conversation (:00-:01)
    bool smart_warp_enabled;        // Compress long silent gaps (>2 min) to ~15s
} app_settings_t;

extern app_settings_t g_settings;

esp_err_t settings_mgr_init(void);
const app_settings_t *settings_mgr_get(void);
esp_err_t settings_mgr_save(const app_settings_t *settings);
esp_err_t settings_mgr_load(app_settings_t *settings);
esp_err_t settings_mgr_set_brightness(uint8_t brightness);
esp_err_t settings_mgr_set_mission(uint8_t mission_id);
esp_err_t settings_mgr_toggle_time_mode(void);

#ifdef __cplusplus
}
#endif
