#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t get_sec;            // Ground Elapsed Time (seconds)
    int64_t local_sec;          // Offset local time (Unix timestamp seconds)
    uint8_t speaker_id;         // Speaker identifier
    bool is_joke;               // True: Render in Bright White (#ffffff); False: CRT Green
    bool is_continuation;       // True: Continuation line of previous utterance (indented)
    char text[160];             // Message content
} terminal_log_entry_t;

void terminal_ui_init(void);
void terminal_ui_timer_handler(void);
void terminal_ui_add_log(int32_t get_sec, int64_t local_sec, uint8_t speaker_id, const char *text, bool is_joke, bool is_continuation);
void terminal_ui_refresh_all_lines(void);
void terminal_ui_update_status(const char *clock_str, const char *mission_str, const char *mode_str, const char *wifi_str);
void terminal_ui_clear(void);
int terminal_ui_get_queue_count(void);
bool terminal_ui_is_typing(void);

#ifdef __cplusplus
}
#endif
