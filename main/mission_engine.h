#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

void mission_engine_init(void);
void mission_engine_start(void);
void mission_engine_handle_touch(int screen_x, int screen_y);
void mission_engine_set_mission(mission_id_t id);
void mission_engine_on_time_synced(void);

#ifdef __cplusplus
}
#endif
