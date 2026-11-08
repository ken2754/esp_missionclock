#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_ACTIVE,
} wifi_state_t;

typedef void (*wifi_state_cb_t)(wifi_state_t state, const char *ip_or_ssid);

esp_err_t wifi_portal_init(wifi_state_cb_t cb);
bool wifi_portal_is_connected(void);
bool wifi_portal_is_ap_active(void);
const char *wifi_portal_get_ip(void);
void wifi_portal_stop_ap(void);

#ifdef __cplusplus
}
#endif
