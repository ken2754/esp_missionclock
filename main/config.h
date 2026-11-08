#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// CYD 2.8" ESP32-2432S028 Pin Configuration
#define LCD_HOST                SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ      (40 * 1000 * 1000)
#define LCD_PIN_NUM_SCLK        14
#define LCD_PIN_NUM_MOSI        13
#define LCD_PIN_NUM_MISO        12
#define LCD_PIN_NUM_DC          2
#define LCD_PIN_NUM_CS          15
#define LCD_PIN_NUM_RST         -1
#define LCD_PIN_NUM_BK_LIGHT    21

// Display dimensions (Landscape)
#define LCD_H_RES               320
#define LCD_V_RES               240

// Terminal Grid Dimensions (Crisp 12-line Retro CRT style)
#define TERM_COLS               40      // 320 / 8 px per character
#define TERM_LOG_ROWS           11      // 11 visible transcript log rows
#define TERM_STATUS_ROW_HEIGHT  25      // 25px status bar at bottom
#define TERM_TOTAL_ROWS         12      // Total logical rows (11 log + 1 status)
#define TERM_CHAR_WIDTH         8
#define TERM_LINE_HEIGHT        19      // (240 - 25) / 11 = ~19.5 px

// CYD 2.8" Touch (XPT2046) Configuration
#define TOUCH_SPI_HOST          SPI3_HOST
#define TOUCH_PIN_NUM_CLK       25
#define TOUCH_PIN_NUM_MOSI      32
#define TOUCH_PIN_NUM_MISO      39
#define TOUCH_PIN_NUM_CS        33
#define TOUCH_PIN_NUM_IRQ       36

// CYD Onboard RGB LED (Active LOW)
#define LED_PIN_RED             4
#define LED_PIN_GREEN           16
#define LED_PIN_BLUE            17

// SoftAP Configuration
#define PORTAL_AP_SSID          "CYD-MissionClock-Setup"
#define PORTAL_AP_PASS          ""          // Open AP for initial configuration
#define PORTAL_AP_CHANNEL       1
#define PORTAL_MAX_CONN         4
#define PORTAL_TIMEOUT_SEC      180         // 3 minutes
#define PORTAL_IP_ADDR          "192.168.4.1"

// Default Settings
#define DEFAULT_TIMEZONE        "JST-9"
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_BRIGHTNESS      100         // 0%, 25%, 50%, 100% loop

// Mission ID Enum
typedef enum {
    MISSION_APOLLO_8 = 0,
    MISSION_APOLLO_11,
    MISSION_APOLLO_13,
    MISSION_APOLLO_17,
    MISSION_COUNT
} mission_id_t;

// Time Display Mode Enum
typedef enum {
    TIME_MODE_LOCAL_OFFSET = 0, // [15:30:15] Offset local current time
    TIME_MODE_HISTORICAL_GET,   // [T-01:15:40] or [T+000:00:15] Historical GET
    TIME_MODE_COUNT
} time_display_mode_t;

// Color Palette Constants
#define COLOR_CRT_BG            lv_color_make(0, 0, 0)          // Pure Black
#define COLOR_CRT_BRIGHT_GREEN  lv_color_make(0, 255, 65)       // P1 Phosphor Bright Green
#define COLOR_CRT_DIM_GREEN     lv_color_make(0, 110, 35)       // CRT Phosphor Glow / Dim Green
#define COLOR_CRT_WHITE         lv_color_make(255, 255, 255)   // Crisp White (Hourly Joke / Easter Egg)

#ifdef __cplusplus
}
#endif
