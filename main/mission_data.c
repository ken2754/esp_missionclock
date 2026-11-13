#include "mission_data.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "miniz.h"

static const char *TAG = "mission_data";

#define FLAG_CONTINUATION 0x80

typedef struct __attribute__((packed)) {
    char magic[4];              // "MISZ"
    uint32_t total_events;
    uint32_t chunk_count;
    uint16_t events_per_chunk;
    uint16_t flags;
} misz_header_t;

typedef struct __attribute__((packed)) {
    uint32_t data_offset;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t event_count;
    uint16_t reserved;
} misz_chunk_dir_t;

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
    uint32_t chunk_count;
    uint16_t events_per_chunk;
} mission_descriptor_t;

// Single-chunk decompression cache in DRAM (avoids holding full transcripts or index tables in RAM)
static SemaphoreHandle_t s_data_mux = NULL;
static tinfl_decompressor *s_decomp = NULL;
static uint8_t *s_chunk_buf = NULL;
static uint32_t s_chunk_buf_cap = 0;
static uint32_t *s_chunk_offsets = NULL;
static uint16_t s_chunk_offsets_cap = 0;

static int s_cached_mission = -1;
static uint32_t s_cached_chunk_idx = 0;
static uint16_t s_cached_chunk_events = 0;

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

static bool load_chunk_locked(mission_id_t id, uint32_t chunk_idx)
{
    mission_descriptor_t *desc = &s_missions[id];
    if (chunk_idx >= desc->chunk_count || !s_decomp || !s_chunk_buf || !s_chunk_offsets) {
        return false;
    }

    const uint8_t *dir_ptr = desc->bin_start + sizeof(misz_header_t) + chunk_idx * sizeof(misz_chunk_dir_t);
    misz_chunk_dir_t dir;
    memcpy(&dir, dir_ptr, sizeof(dir));

    if ((size_t)dir.data_offset + dir.comp_size > (size_t)(desc->bin_end - desc->bin_start)) {
        ESP_LOGE(TAG, "Mission %d chunk %u exceeds binary bounds!", id, (unsigned int)chunk_idx);
        return false;
    }
    if (dir.uncomp_size > s_chunk_buf_cap || dir.event_count > s_chunk_offsets_cap) {
        ESP_LOGE(TAG, "Mission %d chunk %u exceeds cache capacity!", id, (unsigned int)chunk_idx);
        return false;
    }

    tinfl_init(s_decomp);
    size_t in_bytes = dir.comp_size;
    size_t out_bytes = dir.uncomp_size;
    const uint8_t *comp_ptr = desc->bin_start + dir.data_offset;
    tinfl_status status = tinfl_decompress(
        s_decomp,
        comp_ptr,
        &in_bytes,
        s_chunk_buf,
        s_chunk_buf,
        &out_bytes,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
    );

    if (status != TINFL_STATUS_DONE || out_bytes != dir.uncomp_size) {
        ESP_LOGE(TAG, "Failed to decompress mission %d chunk %u (status=%d, out=%zu/%u)",
                 id, (unsigned int)chunk_idx, status, out_bytes, (unsigned int)dir.uncomp_size);
        s_cached_mission = -1;
        return false;
    }

    uint32_t pos = 0;
    for (uint16_t i = 0; i < dir.event_count; i++) {
        if (pos + 6 > dir.uncomp_size) {
            ESP_LOGE(TAG, "Mission %d chunk %u corrupt record header!", id, (unsigned int)chunk_idx);
            s_cached_mission = -1;
            return false;
        }
        s_chunk_offsets[i] = pos;
        uint8_t text_len = s_chunk_buf[pos + 5];
        pos += 6 + text_len;
        if (pos > dir.uncomp_size) {
            ESP_LOGE(TAG, "Mission %d chunk %u corrupt record length!", id, (unsigned int)chunk_idx);
            s_cached_mission = -1;
            return false;
        }
    }

    s_cached_mission = (int)id;
    s_cached_chunk_idx = chunk_idx;
    s_cached_chunk_events = dir.event_count;
    return true;
}

void mission_data_init(void)
{
    if (!s_data_mux) {
        s_data_mux = xSemaphoreCreateMutex();
    }

    s_missions[MISSION_APOLLO_8].bin_start = a08_bin_start;
    s_missions[MISSION_APOLLO_8].bin_end   = a08_bin_end;

    s_missions[MISSION_APOLLO_11].bin_start = a11_bin_start;
    s_missions[MISSION_APOLLO_11].bin_end   = a11_bin_end;

    s_missions[MISSION_APOLLO_13].bin_start = a13_bin_start;
    s_missions[MISSION_APOLLO_13].bin_end   = a13_bin_end;

    s_missions[MISSION_APOLLO_17].bin_start = a17_bin_start;
    s_missions[MISSION_APOLLO_17].bin_end   = a17_bin_end;

    uint32_t max_uncomp_size = 0;
    uint16_t max_events_per_chunk = 0;

    for (int m = 0; m < MISSION_COUNT; m++) {
        mission_descriptor_t *desc = &s_missions[m];
        size_t bin_len = (desc->bin_start && desc->bin_end > desc->bin_start)
                         ? (size_t)(desc->bin_end - desc->bin_start) : 0;
        if (bin_len < sizeof(misz_header_t)) {
            ESP_LOGE(TAG, "Mission %d binary missing or truncated!", m);
            continue;
        }

        misz_header_t hdr;
        memcpy(&hdr, desc->bin_start, sizeof(hdr));
        if (memcmp(hdr.magic, "MISZ", 4) != 0 || hdr.events_per_chunk == 0) {
            ESP_LOGE(TAG, "Mission %d invalid MISZ header!", m);
            continue;
        }

        size_t min_dir_end = sizeof(misz_header_t) + (size_t)hdr.chunk_count * sizeof(misz_chunk_dir_t);
        if (bin_len < min_dir_end) {
            ESP_LOGE(TAG, "Mission %d directory truncated!", m);
            continue;
        }

        desc->event_count = hdr.total_events;
        desc->info.total_events = hdr.total_events;
        desc->chunk_count = hdr.chunk_count;
        desc->events_per_chunk = hdr.events_per_chunk;

        if (hdr.events_per_chunk > max_events_per_chunk) {
            max_events_per_chunk = hdr.events_per_chunk;
        }

        for (uint32_t c = 0; c < hdr.chunk_count; c++) {
            misz_chunk_dir_t dir;
            memcpy(&dir, desc->bin_start + sizeof(misz_header_t) + c * sizeof(misz_chunk_dir_t), sizeof(dir));
            if (dir.uncomp_size > max_uncomp_size) {
                max_uncomp_size = dir.uncomp_size;
            }
        }

        ESP_LOGI(TAG, "Mission %s (%s) ready: %u events in %u chunks (%zu KB compressed)",
                 desc->info.id_str, desc->info.full_name,
                 (unsigned int)desc->event_count, (unsigned int)desc->chunk_count,
                 bin_len / 1024);
    }

    if (max_uncomp_size > 0 && max_events_per_chunk > 0) {
        s_decomp = calloc(1, sizeof(tinfl_decompressor));
        s_chunk_buf = malloc(max_uncomp_size);
        s_chunk_offsets = malloc(sizeof(uint32_t) * max_events_per_chunk);
        if (s_decomp && s_chunk_buf && s_chunk_offsets) {
            s_chunk_buf_cap = max_uncomp_size;
            s_chunk_offsets_cap = max_events_per_chunk;
            ESP_LOGI(TAG, "Decompression cache allocated: %u bytes buffer, %u max events/chunk",
                     (unsigned int)s_chunk_buf_cap, (unsigned int)s_chunk_offsets_cap);
        } else {
            ESP_LOGE(TAG, "Failed to allocate decompression cache!");
        }
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
    if (index >= desc->event_count || desc->events_per_chunk == 0) return false;

    if (s_data_mux) xSemaphoreTake(s_data_mux, portMAX_DELAY);

    uint32_t chunk_idx = index / desc->events_per_chunk;
    uint16_t local_idx = (uint16_t)(index % desc->events_per_chunk);

    if (s_cached_mission != (int)id || s_cached_chunk_idx != chunk_idx) {
        if (!load_chunk_locked(id, chunk_idx)) {
            if (s_data_mux) xSemaphoreGive(s_data_mux);
            return false;
        }
    }

    if (local_idx >= s_cached_chunk_events) {
        if (s_data_mux) xSemaphoreGive(s_data_mux);
        return false;
    }

    const uint8_t *rec = s_chunk_buf + s_chunk_offsets[local_idx];
    int32_t get_sec = 0;
    memcpy(&get_sec, rec, sizeof(int32_t));
    uint8_t spk_flags = rec[4];
    uint8_t text_len = rec[5];

    out_event->get_sec = get_sec;
    out_event->speaker_id = spk_flags & 0x7F;
    out_event->is_continuation = (spk_flags & FLAG_CONTINUATION) != 0;
    out_event->text_len = text_len;
    out_event->text = (const char *)(rec + 6);

    if (s_data_mux) xSemaphoreGive(s_data_mux);
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
