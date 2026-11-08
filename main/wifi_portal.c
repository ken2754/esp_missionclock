#include "wifi_portal.h"
#include "settings_mgr.h"
#include "config.h"
#include "sntp_time.h"
#include "display_driver.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"

static const char *TAG = "wifi_portal";

static bool s_is_ap_active = false;
static bool s_is_connected = false;
static char s_current_ip[32] = "192.168.4.1";
static int s_remaining_sec = PORTAL_TIMEOUT_SEC;
static httpd_handle_t s_http_server = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static wifi_state_cb_t s_user_state_cb = NULL;
static esp_timer_handle_t s_countdown_timer = NULL;

static void configure_sta(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0) return;
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (pass) {
        strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
    }
    ESP_LOGI(TAG, "Configuring STA SSID: '%s', pass_len: %zu", ssid, pass ? strlen(pass) : 0);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
}

static const char HTML_PAGE[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>NASA Mission Clock Setup</title>"
"<style>"
"body{font-family:monospace;background:#050d08;color:#00ff41;margin:0;padding:16px;}"
".card{max-width:520px;margin:0 auto;background:#001408;padding:24px;border-radius:8px;border:1px solid #00551a;box-shadow:0 0 15px rgba(0,255,65,0.2);}"
"h2{margin-top:0;color:#00ff41;font-size:1.3rem;text-align:center;letter-spacing:2px;border-bottom:1px solid #00551a;padding-bottom:10px;}"
"h3{font-size:0.95rem;color:#55ff88;margin:18px 0 6px 0;text-transform:uppercase;}"
"label{display:block;margin-top:12px;font-size:0.85rem;color:#00aa33;font-weight:600;}"
"input,select{width:100%%;padding:10px;margin-top:6px;border-radius:4px;border:1px solid #007722;background:#000a04;color:#00ff41;box-sizing:border-box;font-family:monospace;font-size:0.9rem;}"
"input:focus,select:focus{border-color:#00ff41;outline:none;box-shadow:0 0 8px rgba(0,255,65,0.4);}"
"button{width:100%%;padding:12px;margin-top:24px;background:#008822;color:#ffffff;border:1px solid #00ff41;border-radius:4px;font-weight:bold;cursor:pointer;font-family:monospace;font-size:1rem;}"
"button:hover{background:#00aa33;}"
".note{font-size:0.75rem;color:#008822;margin-top:4px;line-height:1.4;}"
"</style></head><body><div class='card'>"
"<h2>APOLLO MISSION CLOCK</h2>"
"<form action='/save' method='POST'>"
"<h3>[ Wi-Fi Connection ]</h3>"
"<label>Wi-Fi SSID</label><input type='text' name='ssid' value='%s' placeholder='Network name'>"
"<label>Wi-Fi Password</label><input type='password' name='pass' value='%s' placeholder='Password'>"
"<h3>[ Mission Configuration ]</h3>"
"<label>Active Apollo Mission</label>"
"<select name='mission'>"
"<option value='0' %s>Apollo 8 (First to the Moon & Genesis Reading)</option>"
"<option value='1' %s>Apollo 11 (First Lunar Landing & That's One Small Step)</option>"
"<option value='2' %s>Apollo 13 (A Successful Failure & Houston Problem)</option>"
"<option value='3' %s>Apollo 17 (Night Launch, Hold & Last Men on the Moon)</option>"
"</select>"
"<label>Time Display Mode</label>"
"<select name='timemode'>"
"<option value='0' %s>Local Offset Time (Current clock sync)</option>"
"<option value='1' %s>Historical GET Time (T-/T+ Elapsed time)</option>"
"</select>"
"<div class='note'>Tip: Tap display screen to toggle time mode instantly.</div>"
"<label>Hourly Joke & Easter Egg (:00-:01)</label>"
"<select name='joke'>"
"<option value='1' %s>Enabled (Houston time checks & surreal banter in White)</option>"
"<option value='0' %s>Disabled (Historical transcript only)</option>"
"</select>"
"<label>Smart Silence Warp</label>"
"<select name='warp'>"
"<option value='1' %s>Enabled (Fast-forward long silences &gt;2 min to ~15s)</option>"
"<option value='0' %s>Disabled (Strict real-time delay including sleep periods)</option>"
"</select>"
"<h3>[ System & Display ]</h3>"
"<label>Timezone (POSIX)</label><input type='text' name='tz' value='%s' placeholder='JST-9'>"
"<label>Display Brightness</label>"
"<select name='bright'>"
"<option value='100' %s>100%% (Full)</option>"
"<option value='50' %s>50%% (Medium)</option>"
"<option value='25' %s>25%% (Dim)</option>"
"<option value='0' %s>Off</option>"
"</select>"
"<button type='submit'>SAVE &amp; APPLY SETTINGS</button>"
"</form></div></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const app_settings_t *s = settings_mgr_get();
    char *buf = malloc(sizeof(HTML_PAGE) + 2048);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(buf, sizeof(HTML_PAGE) + 2048, HTML_PAGE,
             s->wifi_ssid,
             s->wifi_pass,
             (s->mission_id == 0) ? "selected" : "",
             (s->mission_id == 1) ? "selected" : "",
             (s->mission_id == 2) ? "selected" : "",
             (s->mission_id == 3) ? "selected" : "",
             (s->time_mode == 0) ? "selected" : "",
             (s->time_mode == 1) ? "selected" : "",
             s->hourly_joke_enabled ? "selected" : "",
             !s->hourly_joke_enabled ? "selected" : "",
             s->smart_warp_enabled ? "selected" : "",
             !s->smart_warp_enabled ? "selected" : "",
             s->timezone,
             (s->brightness == 100) ? "selected" : "",
             (s->brightness == 50) ? "selected" : "",
             (s->brightness == 25) ? "selected" : "",
             (s->brightness == 0) ? "selected" : ""
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    char a, b;
    size_t d_idx = 0;
    while (*src && d_idx < dst_len - 1) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            dst[d_idx++] = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            dst[d_idx++] = ' ';
            src++;
        } else {
            dst[d_idx++] = *src++;
        }
    }
    dst[d_idx] = '\0';
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    app_settings_t new_s;
    memcpy(&new_s, settings_mgr_get(), sizeof(app_settings_t));

    char *pair = strtok(buf, "&");
    while (pair != NULL) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char *key = pair;
            char *val = eq + 1;
            char decoded[128];
            url_decode(decoded, val, sizeof(decoded));

            if (strcmp(key, "ssid") == 0) {
                strncpy(new_s.wifi_ssid, decoded, sizeof(new_s.wifi_ssid) - 1);
            } else if (strcmp(key, "pass") == 0) {
                strncpy(new_s.wifi_pass, decoded, sizeof(new_s.wifi_pass) - 1);
            } else if (strcmp(key, "mission") == 0) {
                new_s.mission_id = atoi(decoded);
            } else if (strcmp(key, "timemode") == 0) {
                new_s.time_mode = atoi(decoded);
            } else if (strcmp(key, "joke") == 0) {
                new_s.hourly_joke_enabled = (atoi(decoded) != 0);
            } else if (strcmp(key, "warp") == 0) {
                new_s.smart_warp_enabled = (atoi(decoded) != 0);
            } else if (strcmp(key, "tz") == 0) {
                strncpy(new_s.timezone, decoded, sizeof(new_s.timezone) - 1);
            } else if (strcmp(key, "bright") == 0) {
                new_s.brightness = atoi(decoded);
            }
        }
        pair = strtok(NULL, "&");
    }

    settings_mgr_save(&new_s);
    display_driver_set_backlight(new_s.brightness);
    sntp_time_update_tz(new_s.timezone);

    if (strlen(new_s.wifi_ssid) > 0) {
        configure_sta(new_s.wifi_ssid, new_s.wifi_pass);
        esp_wifi_connect();
    }

    const char resp[] = "<!DOCTYPE html><html><body style='font-family:monospace;background:#050d08;color:#00ff41;padding:30px;text-align:center;'>"
                        "<h2>SETTINGS SAVED!</h2><p>Configuration updated. Returning to Mission Clock...</p>"
                        "<script>setTimeout(function(){window.location.href='/';}, 3000);</script></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void start_webserver(void)
{
    if (s_http_server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &save_uri);
        ESP_LOGI(TAG, "HTTP server started.");
    }
}

static void countdown_timer_cb(void *arg)
{
    if (!s_is_ap_active) return;
    s_remaining_sec--;
    if (s_remaining_sec <= 0) {
        ESP_LOGI(TAG, "AP Timeout reached, stopping AP.");
        wifi_portal_stop_ap();
    }
}

void wifi_portal_stop_ap(void)
{
    if (!s_is_ap_active) return;
    s_is_ap_active = false;
    if (s_countdown_timer) {
        esp_timer_stop(s_countdown_timer);
        esp_timer_delete(s_countdown_timer);
        s_countdown_timer = NULL;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Switched Wi-Fi mode to STA only.");
    if (s_user_state_cb) {
        s_user_state_cb(s_is_connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED, s_current_ip);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected.");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "STA disconnected, reconnecting in 5s...");
                s_is_connected = false;
                if (s_user_state_cb) s_user_state_cb(WIFI_STATE_DISCONNECTED, NULL);
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Client connected to Setup AP.");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(s_current_ip, sizeof(s_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Got IP: %s", s_current_ip);
            s_is_connected = true;
            sntp_time_init(NULL);
            if (s_user_state_cb) {
                s_user_state_cb(WIFI_STATE_CONNECTED, s_current_ip);
            }
        }
    }
}

esp_err_t wifi_portal_init(wifi_state_cb_t cb)
{
    s_user_state_cb = cb;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    const app_settings_t *s = settings_mgr_get();
    bool has_wifi_creds = (strlen(s->wifi_ssid) > 0);

    if (!has_wifi_creds) {
        // Start AP+STA mode
        ESP_LOGI(TAG, "No Wi-Fi credentials found. Starting SoftAP: %s", PORTAL_AP_SSID);
        wifi_config_t ap_config = {
            .ap = {
                .ssid = PORTAL_AP_SSID,
                .ssid_len = strlen(PORTAL_AP_SSID),
                .channel = PORTAL_AP_CHANNEL,
                .password = PORTAL_AP_PASS,
                .max_connection = PORTAL_MAX_CONN,
                .authmode = WIFI_AUTH_OPEN
            },
        };
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        s_is_ap_active = true;

        esp_timer_create_args_t timer_args = {
            .callback = &countdown_timer_cb,
            .name = "portal_countdown"
        };
        esp_timer_create(&timer_args, &s_countdown_timer);
        esp_timer_start_periodic(s_countdown_timer, 1000000); // 1 sec

        if (s_user_state_cb) s_user_state_cb(WIFI_STATE_AP_ACTIVE, PORTAL_AP_SSID);
    } else {
        ESP_LOGI(TAG, "Wi-Fi credentials found for '%s'. Starting STA mode.", s->wifi_ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        configure_sta(s->wifi_ssid, s->wifi_pass);
        if (s_user_state_cb) s_user_state_cb(WIFI_STATE_CONNECTING, s->wifi_ssid);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    start_webserver();

    return ESP_OK;
}

bool wifi_portal_is_connected(void)
{
    return s_is_connected;
}

bool wifi_portal_is_ap_active(void)
{
    return s_is_ap_active;
}

const char *wifi_portal_get_ip(void)
{
    return s_current_ip;
}
