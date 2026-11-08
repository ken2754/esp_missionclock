#include "mission_data.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "mission_data";

// Binary symbols embedded via CMake EMBED_FILES
extern const uint8_t a08_bin_start[] asm("_binary_mission_a08_bin_start");
extern const uint8_t a08_bin_end[]   asm("_binary_mission_a08_bin_end");

extern const uint8_t a11_bin_start[] asm("_binary_mission_a11_bin_start");
extern const uint8_t a11_bin_end[]   asm("_binary_mission_a11_bin_end");

extern const uint8_t a13_bin_start[] asm("_binary_mission_a13_bin_start");
extern const uint8_t a13_bin_end[]   asm("_binary_mission_a13_bin_end");

extern const uint8_t a17_bin_start[] asm("_binary_mission_a17_bin_start");
extern const uint8_t a17_bin_end[]   asm("_binary_mission_a17_bin_end");

typedef struct {
    const uint8_t *bin_start;
    const uint8_t *bin_end;
    mission_info_t info;
    uint32_t event_count;
    const uint8_t **index_table; // Fast O(1) pointer array to each record
} mission_descriptor_t;

static mission_descriptor_t s_missions[MISSION_COUNT] = {
    [MISSION_APOLLO_8] = {
        .info = {
            .id_str = "A08",
            .full_name = "Apollo 8",
            .subtitle = "Genesis & Earthrise",
            .cdr_name = "Borman",
            .cmp_name = "Lovell",
            .lmp_name = "Anders",
            .total_events = 0
        }
    },
    [MISSION_APOLLO_11] = {
        .info = {
            .id_str = "A11",
            .full_name = "Apollo 11",
            .subtitle = "Tranquility Base",
            .cdr_name = "Armstrong",
            .cmp_name = "Collins",
            .lmp_name = "Aldrin",
            .total_events = 0
        }
    },
    [MISSION_APOLLO_13] = {
        .info = {
            .id_str = "A13",
            .full_name = "Apollo 13",
            .subtitle = "Successful Failure",
            .cdr_name = "Lovell",
            .cmp_name = "Swigert",
            .lmp_name = "Haise",
            .total_events = 0
        }
    },
    [MISSION_APOLLO_17] = {
        .info = {
            .id_str = "A17",
            .full_name = "Apollo 17",
            .subtitle = "Night Launch & Hold",
            .cdr_name = "Cernan",
            .cmp_name = "Evans",
            .lmp_name = "Schmitt",
            .total_events = 0
        }
    }
};

void mission_data_init(void)
{
    s_missions[MISSION_APOLLO_8].bin_start = a08_bin_start;
    s_missions[MISSION_APOLLO_8].bin_end   = a08_bin_end;

    s_missions[MISSION_APOLLO_11].bin_start = a11_bin_start;
    s_missions[MISSION_APOLLO_11].bin_end   = a11_bin_end;

    s_missions[MISSION_APOLLO_13].bin_start = a13_bin_start;
    s_missions[MISSION_APOLLO_13].bin_end   = a13_bin_end;

    s_missions[MISSION_APOLLO_17].bin_start = a17_bin_start;
    s_missions[MISSION_APOLLO_17].bin_end   = a17_bin_end;

    for (int m = 0; m < MISSION_COUNT; m++) {
        mission_descriptor_t *desc = &s_missions[m];
        if (!desc->bin_start || desc->bin_start >= desc->bin_end) {
            ESP_LOGE(TAG, "Mission %d binary missing!", m);
            continue;
        }

        const uint8_t *p = desc->bin_start;
        if (memcmp(p, "MISS", 4) != 0) {
            ESP_LOGE(TAG, "Mission %d invalid magic header!", m);
            continue;
        }

        uint32_t count = *(const uint32_t *)(p + 4);
        desc->event_count = count;
        desc->info.total_events = count;

        desc->index_table = malloc(sizeof(const uint8_t *) * count);
        if (!desc->index_table) {
            ESP_LOGE(TAG, "Failed to allocate index table for mission %d (%u events)", m, (unsigned int)count);
            continue;
        }

        const uint8_t *curr = p + 8; // Past magic + count
        for (uint32_t i = 0; i < count; i++) {
            desc->index_table[i] = curr;
            uint8_t text_len = *(curr + 5); // 4 bytes get_sec, 1 byte spk, 1 byte text_len
            curr += 6 + text_len;
            if (curr > desc->bin_end) {
                ESP_LOGW(TAG, "Mission %d record %u exceeds bounds!", m, (unsigned int)i);
                desc->event_count = i;
                desc->info.total_events = i;
                break;
            }
        }
        ESP_LOGI(TAG, "Mission %s (%s) loaded: %u events, binary size: %zu bytes",
                 desc->info.id_str, desc->info.full_name, (unsigned int)desc->event_count,
                 (size_t)(desc->bin_end - desc->bin_start));
    }
}

const mission_info_t *mission_data_get_info(mission_id_t id)
{
    if (id >= MISSION_COUNT) return &s_missions[MISSION_APOLLO_17].info;
    return &s_missions[id].info;
}

uint32_t mission_data_get_count(mission_id_t id)
{
    if (id >= MISSION_COUNT) return 0;
    return s_missions[id].event_count;
}

bool mission_data_get_event(mission_id_t id, uint32_t index, mission_event_t *out_event)
{
    if (id >= MISSION_COUNT || !out_event) return false;
    mission_descriptor_t *desc = &s_missions[id];
    if (index >= desc->event_count || !desc->index_table) return false;

    const uint8_t *rec = desc->index_table[index];
    out_event->get_sec = *(const int32_t *)rec;
    out_event->speaker_id = *(rec + 4);
    out_event->text_len = *(rec + 5);
    out_event->text = (const char *)(rec + 6);
    return true;
}

const char *mission_data_get_speaker_tag(mission_id_t id, uint8_t speaker_id)
{
    switch (speaker_id) {
        case SPK_CDR: return "CDR";
        case SPK_CMP: return "CMP";
        case SPK_LMP: return "LMP";
        case SPK_CC:  return "CC";
        case SPK_PAO: return "PAO";
        case SPK_LCC: return "LCC";
        case SPK_SC:  return "SC";
        default:      return "MSFN";
    }
}
