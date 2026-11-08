#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Speaker IDs
#define SPK_OTHER   0
#define SPK_CDR     1   // Commander
#define SPK_CMP     2   // Command Module Pilot
#define SPK_LMP     3   // Lunar Module Pilot
#define SPK_CC      4   // Capsule Communicator (CAPCOM)
#define SPK_PAO     5   // Public Affairs Officer
#define SPK_LCC     6   // Launch Control Center
#define SPK_SC      7   // Spacecraft Crew

typedef struct {
    int32_t get_sec;        // Ground Elapsed Time in seconds (negative for pre-launch)
    uint8_t speaker_id;     // SPK_*
    uint8_t text_len;       // Length of message string
    const char *text;       // Pointer to text in flash (non null-terminated or copied)
} mission_event_t;

typedef struct {
    const char *id_str;     // "A08", "A11", "A13", "A17"
    const char *full_name;  // "Apollo 8", "Apollo 11", etc.
    const char *subtitle;   // "First to the Moon", "Eagle has landed", etc.
    const char *cdr_name;   // "Borman", "Armstrong", "Lovell", "Cernan"
    const char *cmp_name;   // "Lovell", "Collins", "Swigert", "Evans"
    const char *lmp_name;   // "Anders", "Aldrin", "Haise", "Schmitt"
    uint32_t total_events;
} mission_info_t;

void mission_data_init(void);
const mission_info_t *mission_data_get_info(mission_id_t id);
uint32_t mission_data_get_count(mission_id_t id);
bool mission_data_get_event(mission_id_t id, uint32_t index, mission_event_t *out_event);
const char *mission_data_get_speaker_tag(mission_id_t id, uint8_t speaker_id);

#ifdef __cplusplus
}
#endif
