#include "settings_mgr.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings_mgr";
#define NVS_NAMESPACE "missionclock"

app_settings_t g_settings;

static void set_defaults(app_settings_t *s)
{
    memset(s, 0, sizeof(app_settings_t));
    s->wifi_ssid[0] = '\0';
    s->wifi_pass[0] = '\0';
    strncpy(s->timezone, DEFAULT_TIMEZONE, sizeof(s->timezone) - 1);
    s->brightness = DEFAULT_BRIGHTNESS;
    s->mission_id = MISSION_APOLLO_17; // Default to Apollo 17 (pre-launch countdown & hold!)
    s->time_mode = TIME_MODE_LOCAL_OFFSET;
    s->hourly_joke_enabled = true;
}

esp_err_t settings_mgr_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing flash due to NVS error: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    set_defaults(&g_settings);
    return settings_mgr_load(&g_settings);
}

const app_settings_t *settings_mgr_get(void)
{
    return &g_settings;
}

esp_err_t settings_mgr_load(app_settings_t *settings)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings found in NVS, using defaults.");
        return ESP_OK;
    }

    size_t required_size;

    required_size = sizeof(settings->wifi_ssid);
    if (nvs_get_str(handle, "wifi_ssid", settings->wifi_ssid, &required_size) != ESP_OK) {
        settings->wifi_ssid[0] = '\0';
    }

    required_size = sizeof(settings->wifi_pass);
    if (nvs_get_str(handle, "wifi_pass", settings->wifi_pass, &required_size) != ESP_OK) {
        settings->wifi_pass[0] = '\0';
    }

    required_size = sizeof(settings->timezone);
    if (nvs_get_str(handle, "timezone", settings->timezone, &required_size) != ESP_OK) {
        strncpy(settings->timezone, DEFAULT_TIMEZONE, sizeof(settings->timezone) - 1);
    }

    uint8_t u8_val = 0;
    if (nvs_get_u8(handle, "brightness", &u8_val) == ESP_OK) {
        settings->brightness = u8_val;
    }

    if (nvs_get_u8(handle, "mission_id", &u8_val) == ESP_OK) {
        settings->mission_id = (u8_val < MISSION_COUNT) ? u8_val : MISSION_APOLLO_17;
    }

    if (nvs_get_u8(handle, "time_mode", &u8_val) == ESP_OK) {
        settings->time_mode = (u8_val < TIME_MODE_COUNT) ? u8_val : TIME_MODE_LOCAL_OFFSET;
    }

    if (nvs_get_u8(handle, "joke_en", &u8_val) == ESP_OK) {
        settings->hourly_joke_enabled = (u8_val != 0);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Settings loaded: mission=%d, mode=%d, joke=%d",
             settings->mission_id, settings->time_mode, settings->hourly_joke_enabled);
    return ESP_OK;
}

esp_err_t settings_mgr_save(const app_settings_t *settings)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_str(handle, "wifi_ssid", settings->wifi_ssid);
    nvs_set_str(handle, "wifi_pass", settings->wifi_pass);
    nvs_set_str(handle, "timezone", settings->timezone);
    nvs_set_u8(handle, "brightness", settings->brightness);
    nvs_set_u8(handle, "mission_id", settings->mission_id);
    nvs_set_u8(handle, "time_mode", settings->time_mode);
    nvs_set_u8(handle, "joke_en", settings->hourly_joke_enabled ? 1 : 0);

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        memcpy(&g_settings, settings, sizeof(app_settings_t));
        ESP_LOGI(TAG, "Settings committed to NVS.");
    }
    return ret;
}

esp_err_t settings_mgr_set_brightness(uint8_t brightness)
{
    g_settings.brightness = brightness;
    return settings_mgr_save(&g_settings);
}

esp_err_t settings_mgr_set_mission(uint8_t mission_id)
{
    if (mission_id >= MISSION_COUNT) return ESP_ERR_INVALID_ARG;
    g_settings.mission_id = mission_id;
    return settings_mgr_save(&g_settings);
}

esp_err_t settings_mgr_toggle_time_mode(void)
{
    g_settings.time_mode = (g_settings.time_mode == TIME_MODE_LOCAL_OFFSET) ?
                           TIME_MODE_HISTORICAL_GET : TIME_MODE_LOCAL_OFFSET;
    return settings_mgr_save(&g_settings);
}
