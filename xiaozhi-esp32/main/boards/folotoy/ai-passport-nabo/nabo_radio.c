/* NABO FM: network producer -> bounded compressed ring -> speaker-owner decoder.
 * No network read runs on the I2S owner. No decoded PCM queue or Flash spooling.
 */
#include "nabo_radio.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef NRF_ENGINE_HOST
#include "radio_engine_platform.h"
#else
#include "esp_aac_dec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#endif
#include "muse_settings.h"
#include "muse_wifi.h"
#include "nabo_radio_clock.h"
#include "nabo_radio_decoder.h"
#include "nabo_radio_dsp.h"
#include "nabo_radio_stations.h"
#include "nabo_radio_stream.h"

#define COMPRESSED_BYTES (32 * 1024)
#define PREFILL_BYTES (12 * 1024) /* 1.5 seconds at the catalog's common 64 kbit/s. */
#define REBUFFER_BYTES (4 * 1024)
#define MIN_COMPRESSED_BYTES (8 * 1024)
#define RING_BLOCK_BYTES 1024
#define NETWORK_RESERVE_BYTES (16 * 1024)
#define NETWORK_MIN_BLOCK_BYTES 4096
#define INPUT_BYTES 4096
#define PCM_BYTES (NABO_RADIO_MAX_SAMPLES_PER_FRAME * sizeof(int16_t))
#define OUTPUT_SAMPLES 1153
#define STREAM_READ_TIMEOUT_MS 800
#define MAX_FAVORITES 128
enum { STOPPED, CONNECTING, BUFFERING, PLAYING, WAIT_WIFI, RETRYING, FAILED, PAUSED };
static const char* TAG = "nabo.radio";
static atomic_int s_state, s_station, s_meter, s_rate = 16000, s_local = -1;
static atomic_uint s_generation, s_played;
static atomic_uint s_clock_seconds = 60;
static atomic_bool s_wanted, s_active, s_done = true, s_dirty, s_loud = true, s_noise = true;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static nrf_catalog_t s_catalog;
static uint32_t s_favorites[MAX_FAVORITES], s_recent[10], s_saved_uid;
static uint32_t s_legacy_station;
static bool s_has_legacy_station;
static uint64_t s_legacy_favorites;
static bool s_inited;
// Single network producer / single audio consumer. Publishing byte positions
// with release/acquire keeps each 1 KiB block reusable without a contiguous
// 8-32 KiB allocation, locks, or PCM copies between tasks.
typedef struct {
    uint8_t* block[COMPRESSED_BYTES / RING_BLOCK_BYTES];
    size_t capacity;
    atomic_uint written, consumed;
} radio_ring_t;
#ifndef NRF_ENGINE_HOST
#define ring_block_malloc malloc
#define ring_block_free free
static size_t radio_heap_free(void) { return esp_get_free_heap_size(); }
static size_t radio_heap_largest(void) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#endif
static void ring_delete(radio_ring_t* r) {
    if (!r)
        return;
    for (unsigned i = 0; i < COMPRESSED_BYTES / RING_BLOCK_BYTES; i++)
        ring_block_free(r->block[i]);
    free(r);
}
static radio_ring_t* ring_create(size_t bytes) {
#ifdef NRF_ENGINE_HOST
    if (bytes > host_ring_limit)
        return NULL;
#endif
    radio_ring_t* r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->capacity = bytes;
    atomic_init(&r->written, 0);
    atomic_init(&r->consumed, 0);
    for (unsigned i = 0; i < bytes / RING_BLOCK_BYTES; i++) {
        r->block[i] = ring_block_malloc(RING_BLOCK_BYTES);
        if (!r->block[i]) {
            ring_delete(r);
            return NULL;
        }
    }
    return r;
}
static size_t ring_available(radio_ring_t* r) {
    unsigned consumed = atomic_load_explicit(&r->consumed, memory_order_acquire);
    unsigned written = atomic_load_explicit(&r->written, memory_order_acquire);
    unsigned used = written - consumed;
    return used <= r->capacity ? used : r->capacity;
}
static size_t ring_send(radio_ring_t* r, const void* data, size_t n) {
    unsigned written = atomic_load_explicit(&r->written, memory_order_relaxed);
    unsigned consumed = atomic_load_explicit(&r->consumed, memory_order_acquire);
    size_t space = r->capacity - (unsigned)(written - consumed);
    if (n > space)
        n = space;
    size_t at = 0;
    while (at < n) {
        size_t index = (written + (unsigned)at) % r->capacity;
        size_t offset = index % RING_BLOCK_BYTES;
        size_t chunk = RING_BLOCK_BYTES - offset;
        if (chunk > n - at)
            chunk = n - at;
        memcpy(r->block[index / RING_BLOCK_BYTES] + offset, (const uint8_t*)data + at, chunk);
        at += chunk;
    }
    atomic_store_explicit(&r->written, written + (unsigned)n, memory_order_release);
    return n;
}
static size_t ring_receive(radio_ring_t* r, void* data, size_t n) {
    unsigned consumed = atomic_load_explicit(&r->consumed, memory_order_relaxed);
    unsigned written = atomic_load_explicit(&r->written, memory_order_acquire);
    size_t available = (unsigned)(written - consumed);
    if (n > available)
        n = available;
    size_t at = 0;
    while (at < n) {
        size_t index = (consumed + (unsigned)at) % r->capacity;
        size_t offset = index % RING_BLOCK_BYTES;
        size_t chunk = RING_BLOCK_BYTES - offset;
        if (chunk > n - at)
            chunk = n - at;
        memcpy((uint8_t*)data + at, r->block[index / RING_BLOCK_BYTES] + offset, chunk);
        at += chunk;
    }
    atomic_store_explicit(&r->consumed, consumed + (unsigned)n, memory_order_release);
    return n;
}
static radio_ring_t* s_ring;
static size_t s_ring_bytes;
static TaskHandle_t s_network;
static atomic_bool s_network_ready;
static atomic_uint s_retry_at, s_retry_generation;
static atomic_bool s_release_for_memory;
static int64_t s_sleep_end;
static unsigned s_audio_generation, s_retry_delay = 1, s_tail;
static unsigned s_alloc_retry_delay = 3;
static int16_t s_last;
static atomic_uint s_reconnects, s_open_ms, s_heap_before_open, s_largest_before_open;
static atomic_uint s_stall_reason, s_bitrate_kbps;
static atomic_bool s_heard_audio;
enum {
    NET_READS,
    NET_EAGAINS,
    NET_READ_MAX,
    NET_READ_AT,
    NET_BODY_BYTES,
    NET_BODY_AT,
    NET_BODY_GAP,
    NET_PUBLISHED,
    NET_PUBLISH_AT,
    NET_RING_WAIT,
    NET_TIMEOUT_ERRORS,
    NET_STATS
};
static atomic_uint s_net[NET_STATS];
static unsigned net_now_ms(void) { return (unsigned)(esp_timer_get_time() / 1000); }
#if !defined(NRF_ENGINE_HOST) || defined(NRF_NETWORK_HOST)
static void net_max(unsigned field, unsigned value) {
    unsigned previous = atomic_load(&s_net[field]);
    while (previous < value && !atomic_compare_exchange_weak(&s_net[field], &previous, value)) {
    }
}
#endif
/* Only the speaker owner reads/writes these timestamps and counters. */
static unsigned s_rebuffers, s_rebuffer_ms;
static int64_t s_rebuffer_started;
static bool s_rebuffering;
typedef struct {
    nabo_radio_decoder_t mp3;
    void* aac;
    nabo_radio_dsp_t dsp;
    uint8_t* input;
    int16_t *pcm, *out;
    size_t used, ready, at, id3_left, scanned;
    unsigned rate, codec;
    bool prefilled, leading, drained;
} decode_t;
static decode_t* s_decode; /* Created/freed only by the speaker owner. */
static void decode_delete(decode_t* d) {
    if (!d)
        return;
    nabo_radio_decoder_free(&d->mp3);
    if (d->aac)
        esp_aac_dec_close(d->aac);
    free(d->input);
    free(d->pcm);
    free(d->out);
    free(d);
}

static void rebuffer_begin(void) {
    if (!s_rebuffering) {
        s_rebuffering = true;
        s_rebuffer_started = esp_timer_get_time();
        ++s_rebuffers;
    }
}
static void rebuffer_end(void) {
    if (s_rebuffering) {
        s_rebuffer_ms += (unsigned)((esp_timer_get_time() - s_rebuffer_started) / 1000);
        s_rebuffering = false;
    }
}
void nabo_radio_telemetry(nabo_radio_telemetry_t* out) {
    if (!out)
        return;
    *out = (nabo_radio_telemetry_t){
        .ring = (unsigned)s_ring_bytes,
        .rebuffers = s_rebuffers,
        .rebuffer_ms =
            s_rebuffer_ms +
            (s_rebuffering ? (unsigned)((esp_timer_get_time() - s_rebuffer_started) / 1000) : 0),
        .reconnects = atomic_load(&s_reconnects),
        .open_ms = atomic_load(&s_open_ms),
        .heap_before_open = atomic_load(&s_heap_before_open),
        .largest_before_open = atomic_load(&s_largest_before_open),
        .stall_reason = atomic_load(&s_stall_reason)};
}
void nabo_radio_network_telemetry(nabo_radio_network_telemetry_t* out) {
    if (!out)
        return;
    unsigned reading = atomic_load(&s_net[NET_READ_AT]);
    unsigned body = atomic_load(&s_net[NET_BODY_AT]);
    unsigned published = atomic_load(&s_net[NET_PUBLISH_AT]);
    unsigned now = net_now_ms();
    *out =
        (nabo_radio_network_telemetry_t){.reads = atomic_load(&s_net[NET_READS]),
                                         .eagains = atomic_load(&s_net[NET_EAGAINS]),
                                         .read_max_ms = atomic_load(&s_net[NET_READ_MAX]),
                                         .read_inflight_ms = reading ? now - reading : 0,
                                         .body_bytes = atomic_load(&s_net[NET_BODY_BYTES]),
                                         .body_age_ms = body ? now - body : 0,
                                         .body_gap_max_ms = atomic_load(&s_net[NET_BODY_GAP]),
                                         .published_bytes = atomic_load(&s_net[NET_PUBLISHED]),
                                         .publish_age_ms = published ? now - published : 0,
                                         .ring_wait_ms = atomic_load(&s_net[NET_RING_WAIT]),
                                         .timeout_errors = atomic_load(&s_net[NET_TIMEOUT_ERRORS])};
#ifndef NRF_ENGINE_HOST
    if (s_network) {
        TaskStatus_t task;
        vTaskGetInfo(s_network, &task, pdFALSE, eInvalid);
        out->task_state = (unsigned)task.eCurrentState;
        out->runtime_us = (unsigned)task.ulRunTimeCounter;
        out->stack = (unsigned)uxTaskGetStackHighWaterMark(s_network);
    }
#endif
}
bool nabo_radio_tuning_noise(void) {
    unsigned state = nabo_radio_state();
    return nabo_radio_noise() && nabo_radio_wanted() && !atomic_load(&s_heard_audio) &&
           (state == CONNECTING || state == BUFFERING || state == RETRYING);
}
static int64_t telemetry_open_begin(void) {
    atomic_store(&s_heap_before_open, (unsigned)radio_heap_free());
    atomic_store(&s_largest_before_open, (unsigned)radio_heap_largest());
    atomic_store(&s_open_ms, 0);
    return esp_timer_get_time();
}
static void telemetry_open_end(int64_t started) {
    atomic_store(&s_open_ms, (unsigned)((esp_timer_get_time() - started) / 1000));
}
static void telemetry_reconnect(unsigned reason) {
    atomic_fetch_add(&s_reconnects, 1);
    atomic_store(&s_stall_reason, reason);
}
static unsigned network_stall_ms(size_t buffered, unsigned bitrate_kbps) {
    if (!bitrate_kbps)
        bitrate_kbps = 64;
    unsigned ms = (unsigned)(buffered * 8 / bitrate_kbps);
    /* EAGAIN also covers TLS WANT_READ. A small/depleted ring is not proof
     * that the TLS session is dead. Retain the 03 eight-second grace instead
     * of closing a potentially useful session after only a few poll timeouts. */
    return ms < 8000 ? 8000 : ms;
}
/* The owner schedules retries. Bind the deadline to the old generation so a
 * simultaneous UI station selection cannot inherit an old failure's delay. */
static void schedule_retry(unsigned delay_ms) {
    atomic_store(&s_retry_generation, s_audio_generation);
    atomic_store(&s_retry_at, (uint32_t)(esp_timer_get_time() / 1000) + delay_ms);
}
static void allocation_backoff(void) {
    if (atomic_load(&s_generation) != s_audio_generation) {
        s_alloc_retry_delay = 3;
        return;
    }
    schedule_retry(s_alloc_retry_delay * 1000);
    if (s_alloc_retry_delay < 30) {
        s_alloc_retry_delay *= 2;
        if (s_alloc_retry_delay > 30)
            s_alloc_retry_delay = 30;
    }
}

int nabo_radio_station_count(void) {
    return s_catalog.data ? (int)s_catalog.count : NABO_RADIO_STATION_COUNT;
}
const nrf_catalog_t* nabo_radio_catalog(void) { return &s_catalog; }
bool nabo_radio_station_info(int i, nrf_station_t* out) {
    if (s_catalog.data)
        return nrf_catalog_get(&s_catalog, i, out);
    if (!out || i < 0 || i >= NABO_RADIO_STATION_COUNT)
        return false;
    *out = (nrf_station_t){.uid = (uint32_t)i + 1,
                           .name = nabo_radio_stations[i].name,
                           .url = nabo_radio_stations[i].url,
                           .py = "",
                           .region = nabo_radio_categories[nabo_radio_stations[i].category],
                           .region_id = nabo_radio_stations[i].category,
                           .types = 1u << 10};
    return true;
}
static uint32_t uid(int i) {
    nrf_station_t s;
    return nabo_radio_station_info(i, &s) ? s.uid : 0;
}
static int by_uid(uint32_t id) {
    if (!id)
        return -1;
    for (int i = 0; i < nabo_radio_station_count(); i++)
        if (uid(i) == id)
            return i;
    return -1;
}
bool nabo_radio_bind_catalog(const void* data, size_t size) {
    if (atomic_load(&s_active) || s_catalog.data)
        return s_catalog.data != NULL;
    if (!nrf_catalog_init(&s_catalog, data, size))
        return false;
    // Migrate the old 47-entry bitmask by URL/name, never by the new sort order.
    for (int j = 0; j < NABO_RADIO_STATION_COUNT; j++)
        if (s_legacy_favorites & (UINT64_C(1) << j))
            for (int i = 0; i < (int)s_catalog.count; i++) {
                nrf_station_t s;
                nrf_catalog_get(&s_catalog, i, &s);
                const char* old_live = strstr(nabo_radio_stations[j].url, "/live/");
                uint32_t qt = old_live ? (uint32_t)strtoul(old_live + 6, NULL, 10) : 0;
                if ((qt && qt == s.qt) || !strcmp(s.name, nabo_radio_stations[j].name)) {
                    if (!nabo_radio_favorite(i))
                        nabo_radio_toggle_favorite(i);
                    break;
                }
            }
    int i = by_uid(s_saved_uid);
    if (i < 0 && !s_saved_uid && s_has_legacy_station &&
        s_legacy_station < NABO_RADIO_STATION_COUNT) {
        const char* url = nabo_radio_stations[s_legacy_station].url;
        const char* live = strstr(url, "/live/");
        uint32_t qt = live ? (uint32_t)strtoul(live + 6, NULL, 10) : 0;
        for (int j = 0; j < (int)s_catalog.count; j++) {
            nrf_station_t station;
            nrf_catalog_get(&s_catalog, j, &station);
            if ((qt && qt == station.qt) ||
                !strcmp(station.name, nabo_radio_stations[s_legacy_station].name)) {
                i = j;
                break;
            }
        }
    }
    if (i >= 0)
        atomic_store(&s_station, i);
    else
        atomic_store(&s_station, 0);
    atomic_store(&s_dirty, true);
    return true;
}
const char* nabo_radio_station_name(int i) {
    nrf_station_t s;
    return nabo_radio_station_info(i, &s) ? s.name : "";
}
int nabo_radio_station_category(int i) {
    nrf_station_t s;
    return nabo_radio_station_info(i, &s) ? (int)s.region_id : -1;
}
int nabo_radio_category_count(void) {
    return s_catalog.data ? (int)s_catalog.regions : NABO_RADIO_CATEGORY_COUNT;
}
const char* nabo_radio_category_name(int i) {
    return s_catalog.data                            ? nrf_catalog_region(&s_catalog, (unsigned)i)
           : i >= 0 && i < NABO_RADIO_CATEGORY_COUNT ? nabo_radio_categories[i]
                                                     : "";
}
int nabo_radio_current_station(void) { return atomic_load(&s_station); }
bool nabo_radio_available(void) { return nabo_radio_station_count() > 0; }
bool nabo_radio_wanted(void) { return atomic_load(&s_wanted); }
bool nabo_radio_active(void) { return atomic_load(&s_active); }
unsigned nabo_radio_state(void) { return (unsigned)atomic_load(&s_state); }
unsigned nabo_radio_output_rate(void) { return (unsigned)atomic_load(&s_rate); }
unsigned nabo_radio_buffered_bytes(void) { return s_ring ? (unsigned)ring_available(s_ring) : 0; }
int nabo_radio_meter(void) { return atomic_load(&s_meter); }
bool nabo_radio_loudness(void) { return atomic_load(&s_loud); }
void nabo_radio_set_loudness(bool on) {
    atomic_store(&s_loud, on);
    atomic_store(&s_dirty, true);
}
bool nabo_radio_noise(void) { return atomic_load(&s_noise); }
unsigned nabo_radio_clock_seconds(void) { return atomic_load(&s_clock_seconds); }
void nabo_radio_set_clock_seconds(unsigned seconds) {
    if (seconds != 0 && seconds != 60 && seconds != 180 && seconds != 300 && seconds != 600)
        return;
    if (atomic_exchange(&s_clock_seconds, seconds) != seconds) {
        atomic_store(&s_dirty, true);
        app_nabo_radio_store_clock(seconds);
    }
}
void nabo_radio_set_noise(bool on) {
    atomic_store(&s_noise, on);
    atomic_store(&s_dirty, true);
}
int nabo_radio_local(void) { return atomic_load(&s_local); }
void nabo_radio_set_local(int i) {
    if (i >= -1 && i < nabo_radio_category_count()) {
        atomic_store(&s_local, i);
        atomic_store(&s_dirty, true);
    }
}
void nabo_radio_set_sleep(unsigned minutes) {
    if (minutes > 720)
        minutes = 720;
    portENTER_CRITICAL(&s_lock);
    s_sleep_end = minutes ? esp_timer_get_time() + (int64_t)minutes * 60000000 : 0;
    portEXIT_CRITICAL(&s_lock);
}
unsigned nabo_radio_sleep_remaining(void) {
    portENTER_CRITICAL(&s_lock);
    int64_t end = s_sleep_end;
    portEXIT_CRITICAL(&s_lock);
    int64_t remain = end - esp_timer_get_time();
    return end && remain > 0 ? (unsigned)((remain + 999999) / 1000000) : 0;
}
const char* nabo_radio_state_text(void) {
    static const char* const text[] = {"按 OK 收听",       "连接中…",    "缓冲中…",
                                       "播放中",           "等待 Wi-Fi", "信号不好，重连中",
                                       "这个台暂时连不上", "已暂停"};
    unsigned i = nabo_radio_state();
    return text[i < 8 ? i : 0];
}
bool nabo_radio_favorite(int i) {
    uint32_t id = uid(i);
    if (!id)
        return false;
    bool yes = false;
    portENTER_CRITICAL(&s_lock);
    for (unsigned j = 0; j < MAX_FAVORITES; j++)
        if (s_favorites[j] == id) {
            yes = true;
            break;
        }
    portEXIT_CRITICAL(&s_lock);
    return yes;
}
void nabo_radio_toggle_favorite(int i) {
    uint32_t id = uid(i);
    if (!id)
        return;
    int empty = -1;
    portENTER_CRITICAL(&s_lock);
    for (unsigned j = 0; j < MAX_FAVORITES; j++) {
        if (s_favorites[j] == id) {
            s_favorites[j] = 0;
            empty = -2;
            break;
        }
        if (!s_favorites[j] && empty < 0)
            empty = (int)j;
    }
    if (empty >= 0)
        s_favorites[empty] = id;
    portEXIT_CRITICAL(&s_lock);
    atomic_store(&s_dirty, true);
}
int nabo_radio_recent(int pos) {
    if (pos < 0 || pos >= 10)
        return -1;
    portENTER_CRITICAL(&s_lock);
    uint32_t id = s_recent[pos];
    portEXIT_CRITICAL(&s_lock);
    return by_uid(id);
}
static void remember(int i) {
    uint32_t id = uid(i);
    portENTER_CRITICAL(&s_lock);
    int at = 9;
    for (int j = 0; j < 10; j++)
        if (s_recent[j] == id) {
            at = j;
            break;
        }
    for (int j = at; j > 0; j--)
        s_recent[j] = s_recent[j - 1];
    s_recent[0] = id;
    portEXIT_CRITICAL(&s_lock);
    atomic_store(&s_dirty, true);
}
static void save_settings(void) {
    if (!atomic_exchange(&s_dirty, false))
        return;
    uint32_t fav[MAX_FAVORITES], recent[10];
    portENTER_CRITICAL(&s_lock);
    memcpy(fav, s_favorites, sizeof(fav));
    memcpy(recent, s_recent, sizeof(recent));
    portEXIT_CRITICAL(&s_lock);
    nvs_handle_t h;
    if (nvs_open("nabo_radio", NVS_READWRITE, &h) != ESP_OK) {
        atomic_store(&s_dirty, true);
        return;
    }
    esp_err_t e = nvs_set_blob(h, "favorites2", fav, sizeof(fav));
    if (!e)
        e = nvs_set_blob(h, "recent2", recent, sizeof(recent));
    if (!e)
        e = nvs_set_u32(h, "station_uid", uid(nabo_radio_current_station()));
    if (!e)
        e = nvs_set_u8(h, "loudness", nabo_radio_loudness());
    if (!e)
        e = nvs_set_u8(h, "noise", nabo_radio_noise());
    if (!e)
        e = nvs_set_u8(h, "autoplay", nabo_radio_wanted());
    if (!e)
        e = nvs_set_i32(h, "local", nabo_radio_local());
    if (!e)
        e = nvs_set_u32(h, "clock_sec", nabo_radio_clock_seconds());
    if (!e)
        e = nvs_commit(h);
    nvs_close(h);
    if (e)
        atomic_store(&s_dirty, true);
}
void nabo_radio_init(void) {
    if (s_inited)
        return;
    nvs_handle_t h;
    if (nvs_open("nabo_radio", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_favorites);
        bool migrated = nvs_get_blob(h, "favorites2", s_favorites, &n) == ESP_OK;
        n = sizeof(s_recent);
        nvs_get_blob(h, "recent2", s_recent, &n);
        nvs_get_u32(h, "station_uid", &s_saved_uid);
        s_has_legacy_station = nvs_get_u32(h, "station", &s_legacy_station) == ESP_OK;
        if (!migrated)
            nvs_get_u64(h, "favorites", &s_legacy_favorites);
        uint8_t b;
        if (!nvs_get_u8(h, "loudness", &b))
            atomic_store(&s_loud, b != 0);
        if (!nvs_get_u8(h, "noise", &b))
            atomic_store(&s_noise, b != 0);
        if (!nvs_get_u8(h, "autoplay", &b))
            atomic_store(&s_wanted, b != 0);
        uint32_t clock;
        if (!nvs_get_u32(h, "clock_sec", &clock) &&
            (clock == 0 || clock == 60 || clock == 180 || clock == 300 || clock == 600))
            atomic_store(&s_clock_seconds, clock);
        int32_t local;
        if (!nvs_get_i32(h, "local", &local))
            atomic_store(&s_local, local);
        nvs_close(h);
    }
    s_inited = true;
    ESP_LOGI(TAG,
             "Helix DNA Client technology included. Copyright (c) RealNetworks, Inc., 1995-2002. "
             "All rights reserved.");
}
void nabo_radio_idle_tick(void) {
    portENTER_CRITICAL(&s_lock);
    bool expired = s_sleep_end && esp_timer_get_time() >= s_sleep_end;
    if (expired)
        s_sleep_end = 0;
    portEXIT_CRITICAL(&s_lock);
    if (expired)
        nabo_radio_stop();
    /* A user can cancel while no pipeline exists between retry attempts.
     * Close the wait on this owner thread, even when read/finish will not run. */
    if (!nabo_radio_wanted() || atomic_load(&s_generation) != s_audio_generation)
        rebuffer_end();
    if (!nabo_radio_active())
        save_settings();
}
void nabo_radio_select_station(int i) {
    if (i < 0 || i >= nabo_radio_station_count())
        return;
    atomic_store(&s_station, i);
    atomic_fetch_add(&s_generation, 1);
    atomic_store(&s_wanted, true);
    atomic_store(&s_dirty, true);
    s_retry_at = 0;
}
void nabo_radio_toggle_play(void) {
    atomic_store(&s_dirty, true);
    if (atomic_load(&s_wanted)) {
        atomic_store(&s_wanted, false);
        atomic_store(&s_state, PAUSED);
    } else {
        atomic_fetch_add(&s_generation, 1);
        atomic_store(&s_wanted, true);
        s_retry_at = 0;
    }
}
void nabo_radio_stop(void) {
    atomic_store(&s_dirty, true);
    atomic_store(&s_wanted, false);
    atomic_store(&s_state, STOPPED);
}
bool nabo_radio_due(void) {
    if (!nabo_radio_wanted() || nabo_radio_active())
        return false;
    if (!muse_wifi_connected()) {
        atomic_store(&s_state, WAIT_WIFI);
        return false;
    }
    if (!muse_settings_speaker_on()) {
        nabo_radio_stop();
        return false;
    }
    uint32_t deadline = atomic_load(&s_retry_at);
    return atomic_load(&s_retry_generation) != atomic_load(&s_generation) || !deadline ||
           (int32_t)((uint32_t)(esp_timer_get_time() / 1000) - deadline) >= 0;
}
static bool cancelled(unsigned gen) {
    return !nabo_radio_wanted() || atomic_load(&s_generation) != gen || !muse_settings_speaker_on();
}
#if !defined(NRF_ENGINE_HOST) || defined(NRF_NETWORK_HOST)
typedef struct {
    nabo_icy_t icy;
    bool invalid;
} headers_t;
#ifndef NRF_ENGINE_HOST
static int radio_socket_receive_timeout(int fd, unsigned ms) {
    struct timeval timeout = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}
#endif
static bool configure_stream_timeout(esp_http_client_handle_t http) {
    if (esp_http_client_set_timeout_ms(http, STREAM_READ_TIMEOUT_MS) != ESP_OK)
        return false;
    int fd = esp_http_client_get_socket(http);
    /* set_timeout_ms changes the client's poll timeout, not the receive
     * timeout installed on the socket at connect. Keep 5 s for open/TLS,
     * then apply the shorter per-recv wait as well. Neither is an overall
     * deadline for an HTTP read spanning several transport/TLS fragments. */
    return fd >= 0 && radio_socket_receive_timeout(fd, STREAM_READ_TIMEOUT_MS) == 0;
}
static bool network_audio_stalled(int64_t last_audio, bool qtfm) {
    unsigned timeout = network_stall_ms(ring_available(s_ring), atomic_load(&s_bitrate_kbps));
    /* These sources deliver legal bursts with long quiet gaps. This is a
     * connection-local grace, never an estimate from cross-connection body
     * diagnostics, and does not change the HTTP open or receive budgets. */
    if (qtfm && timeout < 20000)
        timeout = 20000;
    return esp_timer_get_time() - last_audio >= (int64_t)timeout * 1000;
}
static void network_backoff(unsigned gen, unsigned* delay_ms) {
    int64_t until = esp_timer_get_time() + (int64_t)*delay_ms * 1000;
    while (!cancelled(gen) && esp_timer_get_time() < until)
        vTaskDelay(pdMS_TO_TICKS(50));
    if (*delay_ms < 8000)
        *delay_ms *= 2;
}
static esp_err_t on_http(esp_http_client_event_t* ev) {
    headers_t* h = ev->user_data;
    if (!h)
        return ESP_OK;
    if (ev->event_id == HTTP_EVENT_ON_DATA && ev->data_len > 0 && !h->invalid &&
        esp_http_client_get_status_code(ev->client) == 200) {
        unsigned now = net_now_ms();
        unsigned previous = atomic_exchange(&s_net[NET_BODY_AT], now);
        if (previous)
            net_max(NET_BODY_GAP, now - previous);
        atomic_fetch_add(&s_net[NET_BODY_BYTES], (unsigned)ev->data_len);
        return ESP_OK;
    }
    if (ev->event_id != HTTP_EVENT_ON_HEADER)
        return ESP_OK;
    if (!strcasecmp(ev->header_key, "Date"))
        nrf_clock_http_date(ev->header_value, esp_timer_get_time());
    if (!strcasecmp(ev->header_key, "icy-metaint")) {
        char* end;
        errno = 0;
        unsigned long n = strtoul(ev->header_value, &end, 10);
        if (errno || *end || !n || n > 1048576)
            h->invalid = true;
        else
            nabo_icy_init(&h->icy, n);
    }
    if (!strcasecmp(ev->header_key, "Content-Type") &&
        (strstr(ev->header_value, "mpegurl") || strstr(ev->header_value, "text/html")))
        h->invalid = true;
    return ESP_OK;
}
static void network_worker(void* arg) {
    // The owner reserves this task's contiguous stack before selecting a ring
    // size. It publishes the completed pipeline before this task touches it.
    while (!atomic_load_explicit(&s_network_ready, memory_order_acquire))
        vTaskDelay(1);
    unsigned gen = (unsigned)(uintptr_t)arg;
    char url[384];
    nrf_station_t station;
    bool fail = !nabo_radio_station_info(nabo_radio_current_station(), &station) ||
                !nrf_catalog_url(&station, url, sizeof(url));
    esp_http_client_handle_t http = NULL;
    headers_t headers = {0};
    unsigned retry_ms = 1000;
    if (fail)
        goto done;
    while (!cancelled(gen)) {
        while (!cancelled(gen) && !muse_wifi_connected()) {
            atomic_store(&s_stall_reason, NABO_RADIO_STALL_WIFI);
            atomic_store(&s_state, WAIT_WIFI);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (cancelled(gen))
            break;
        /* After cleanup, allow TCP/Wi-Fi allocations to settle before another
         * TLS handshake. Keep the decoder, reservoir, stack and compressed
         * ring: rebuilding those cannot manufacture the missing heap. */
        bool memory_wait = false;
        int64_t memory_started = esp_timer_get_time();
        while (!cancelled(gen) && muse_wifi_connected() &&
               (radio_heap_free() < NETWORK_RESERVE_BYTES ||
                radio_heap_largest() < NETWORK_MIN_BLOCK_BYTES)) {
            if (!memory_wait) {
                ESP_LOGW(TAG, "network waiting for memory: heap=%u largest=%u",
                         (unsigned)radio_heap_free(), (unsigned)radio_heap_largest());
                memory_wait = true;
            }
            atomic_store(&s_stall_reason, NABO_RADIO_STALL_MEMORY);
            atomic_store(&s_state, RETRYING);
            if (esp_timer_get_time() - memory_started >= 10000000) {
                /* A retained ring cannot shrink by being read empty. If the
                 * reserve never returns, release once via the owner and use
                 * its bounded allocation retry, rather than wait silently
                 * forever or hammer TLS with an unsafe memory budget. */
                ESP_LOGW(TAG, "network memory wait expired; releasing pipeline");
                atomic_store(&s_release_for_memory, true);
                fail = true;
                goto done;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (cancelled(gen))
            break;
        if (!muse_wifi_connected())
            continue;
        memset(&headers, 0, sizeof(headers));
        int64_t open_started = telemetry_open_begin();
        bool first_packet = true;
        unsigned stall_reason = NABO_RADIO_STALL_OPEN;
        esp_http_client_config_t cfg = {.url = url,
                                        .crt_bundle_attach = esp_crt_bundle_attach,
                                        .timeout_ms = 5000,
                                        .buffer_size = 1024,
                                        .buffer_size_tx = 512,
                                        .event_handler = on_http,
                                        .user_data = &headers,
                                        .max_redirection_count = 3};
        http = esp_http_client_init(&cfg);
        if (!http) {
            telemetry_reconnect(NABO_RADIO_STALL_OPEN);
            atomic_store(&s_state, RETRYING);
            network_backoff(gen, &retry_ms);
            continue;
        }
        esp_http_client_set_header(http, "Icy-MetaData", "0");
        esp_http_client_set_header(http, "User-Agent", "NABO-FM/0.2");
        esp_err_t open_error = ESP_OK;
        int64_t length = -1;
        int status = 0;
        // open/fetch_headers are the streaming API: max_redirection_count
        // only follows redirects inside perform(), which cannot return for
        // a live stream. Regional stations use redirectors to pick a server.
        for (unsigned redirects = 0; !cancelled(gen); redirects++) {
            memset(&headers, 0, sizeof(headers));
            open_error = esp_http_client_open(http, 0);
            length = open_error == ESP_OK ? esp_http_client_fetch_headers(http) : -1;
            status = esp_http_client_get_status_code(http);
            bool redirect =
                status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
            if (open_error != ESP_OK || length < 0 || !redirect || redirects >= 3)
                break;
            open_error = esp_http_client_set_redirection(http);
            if (open_error != ESP_OK)
                break;
            esp_http_client_close(http);
        }
        bool opened = open_error == ESP_OK && length >= 0 && status == 200 && !headers.invalid;
        if (!opened)
            ESP_LOGW(TAG, "stream open failed: err=%d status=%d header=%d invalid=%d",
                     (int)open_error, status, length >= 0, headers.invalid);
        if (opened && !configure_stream_timeout(http)) {
            opened = false;
            stall_reason = NABO_RADIO_STALL_TIMEOUT_SETUP;
            atomic_fetch_add(&s_net[NET_TIMEOUT_ERRORS], 1);
            ESP_LOGW(TAG, "stream receive timeout setup failed; retaining pipeline for retry");
        }
        if (opened) {
            ESP_LOGI(TAG, "stream connected: status=%d", status);
            int64_t last_audio = esp_timer_get_time();
            while (!cancelled(gen) && muse_wifi_connected()) {
                uint8_t raw[1024], clean[1024];
                size_t low = s_ring_bytes / 2;
                if (low > 4096)
                    low = 4096;
                size_t request = ring_available(s_ring) < low ? 256 : sizeof(raw);
                unsigned started = net_now_ms();
                atomic_store(&s_net[NET_READ_AT], started);
                atomic_fetch_add(&s_net[NET_READS], 1);
                int n = esp_http_client_read(http, (char*)raw, (int)request);
                net_max(NET_READ_MAX, net_now_ms() - started);
                atomic_store(&s_net[NET_READ_AT], 0);
                if (cancelled(gen))
                    break;
                if (n == -ESP_ERR_HTTP_EAGAIN) {
                    atomic_fetch_add(&s_net[NET_EAGAINS], 1);
                    if (network_audio_stalled(last_audio, station.qt != 0)) {
                        stall_reason = NABO_RADIO_STALL_EAGAIN;
                        break;
                    }
                    continue;
                }
                if (n <= 0) {
                    stall_reason = NABO_RADIO_STALL_READ;
                    break;
                }
                if (first_packet) {
                    telemetry_open_end(open_started);
                    first_packet = false;
                }
                size_t bytes = 0;
                if (!nabo_icy_feed(&headers.icy, raw, (size_t)n, clean, sizeof(clean), &bytes)) {
                    stall_reason = NABO_RADIO_STALL_FORMAT;
                    break;
                }
                size_t at = 0;
                while (at < bytes && !cancelled(gen)) {
                    size_t sent = ring_send(s_ring, clean + at, bytes - at);
                    at += sent;
                    if (sent) {
                        atomic_fetch_add(&s_net[NET_PUBLISHED], (unsigned)sent);
                        atomic_store(&s_net[NET_PUBLISH_AT], net_now_ms());
                    } else {
                        /* Preserve received bytes while there is room, even
                         * after Wi-Fi drops, but do not get stuck waiting for
                         * the consumer on a disconnected source. Any bounded
                         * remainder here has not been counted as published. */
                        if (!muse_wifi_connected())
                            break;
                        unsigned waiting = net_now_ms();
                        vTaskDelay(pdMS_TO_TICKS(10));
                        atomic_fetch_add(&s_net[NET_RING_WAIT], net_now_ms() - waiting);
                    }
                }
                if (bytes) {
                    /* Publication can wait on a full ring. That consumer
                     * backpressure is not source silence. */
                    last_audio = esp_timer_get_time();
                    retry_ms = 1000;
                } else if (network_audio_stalled(last_audio, station.qt != 0)) {
                    /* Positive HTTP body can contain only ICY metadata. It
                     * must not keep a connection without audio alive forever. */
                    stall_reason = NABO_RADIO_STALL_EAGAIN;
                    break;
                }
            }
        }
        esp_http_client_cleanup(http);
        http = NULL;
        if (cancelled(gen))
            break;
        if (!muse_wifi_connected())
            stall_reason = NABO_RADIO_STALL_WIFI;
        telemetry_reconnect(stall_reason);
        // Keep the compressed ring and reservoir intact during reconnects. The
        // speaker drains buffered frames while this task performs DNS/HTTP/TLS.
        atomic_store(&s_state, muse_wifi_connected() ? RETRYING : WAIT_WIFI);
        /* A failed open is a network failure, even with an empty ring. Keep
         * this pipeline alive and retry with a bounded rate; ending it here
         * caused the owner to repeatedly rebuild all workspaces under pressure. */
        network_backoff(gen, &retry_ms);
    }
done:
    atomic_store(&s_net[NET_READ_AT], 0);
    if (http)
        esp_http_client_cleanup(http);
    if (fail && !cancelled(gen))
        atomic_store(&s_state, FAILED);
    atomic_store_explicit(&s_done, true, memory_order_release);
    for (;;)
        vTaskSuspend(NULL);
}
#else
static void network_worker(void* arg) { (void)arg; }
#endif
bool nabo_radio_begin(void) {
    if (!nabo_radio_due() || !app_nabo_radio_network_begin())
        return false;
    unsigned generation = atomic_load(&s_generation);
    if (s_audio_generation != generation) {
        for (unsigned i = 0; i < NET_STATS; ++i)
            atomic_store(&s_net[i], 0);
        s_rebuffers = s_rebuffer_ms = 0;
        s_rebuffering = false;
        atomic_store(&s_reconnects, 0);
        atomic_store(&s_open_ms, 0);
        atomic_store(&s_heap_before_open, 0);
        atomic_store(&s_largest_before_open, 0);
        atomic_store(&s_stall_reason, NABO_RADIO_STALL_NONE);
        atomic_store(&s_bitrate_kbps, 64);
        atomic_store(&s_heard_audio, false);
        s_alloc_retry_delay = 3;
    }
    s_audio_generation = generation;
    atomic_store(&s_release_for_memory, false);
    save_settings();
    atomic_store(&s_state, CONNECTING);
    const char* stage = "decode workspace";
    s_decode = calloc(1, sizeof(*s_decode));
    if (!s_decode)
        goto failed;
    stage = "network task";
    atomic_store(&s_network_ready, false);
    if (xTaskCreate(network_worker, "fm_network", 8192, (void*)(uintptr_t)s_audio_generation, 4,
                    &s_network) != pdPASS)
        goto failed;
    nrf_station_t station;
    stage = "station format";
    if (!nabo_radio_station_info(nabo_radio_current_station(), &station) || station.codec > 1)
        goto failed;
    s_decode->codec = station.codec;
    s_decode->leading = true;
    stage = "codec workspace";
    if (station.codec == 1) {
        esp_aac_dec_cfg_t cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        if (esp_aac_dec_open(&cfg, sizeof(cfg), &s_decode->aac) != ESP_AUDIO_ERR_OK)
            goto failed;
    } else if (!nabo_radio_decoder_init(&s_decode->mp3))
        goto failed;
    /* On a voice -> radio return there may be enough total RAM, but the
     * largest hole can be only 18 KiB. Do not spend 11 KiB of that hole on a
     * combined decode_t before the 8 KiB task stack and 8.5 KiB Helix block.
     * Keep the original frame capacities in independent, smaller allocations
     * after reserving those larger workspaces. Publish nothing until complete. */
    stage = "frame buffers";
    s_decode->pcm = malloc(PCM_BYTES);
    s_decode->input = malloc(INPUT_BYTES);
    s_decode->out = malloc(OUTPUT_SAMPLES * sizeof(*s_decode->out));
    if (!s_decode->input || !s_decode->pcm || !s_decode->out)
        goto failed;
    // Voice task teardown and the animated view leave fragmented C3 SRAM.
    // Allocate codec workspaces and the contiguous network stack first; leave
    // room for HTTP/I2S instead of consuming every remaining byte in a ring.
    stage = "compressed buffer";
    for (size_t bytes = COMPRESSED_BYTES; bytes >= MIN_COMPRESSED_BYTES; bytes /= 2) {
        if (radio_heap_free() < bytes + NETWORK_RESERVE_BYTES)
            continue;
        s_ring = ring_create(bytes);
        // Include the real per-block allocator overhead and retain a block
        // large enough for network receive buffers. A successful ring must
        // not leave HTTP/TCP fighting over the last 1 KiB fragments.
        if (s_ring && (radio_heap_free() < NETWORK_RESERVE_BYTES ||
                       radio_heap_largest() < NETWORK_MIN_BLOCK_BYTES)) {
            ring_delete(s_ring);
            s_ring = NULL;
        }
        if (s_ring) {
            s_ring_bytes = bytes;
            break;
        }
    }
    if (!s_ring)
        goto failed;
    atomic_store(&s_done, false);
    atomic_store(&s_active, true);
    s_tail = 0;
    s_last = 0;
    atomic_store_explicit(&s_network_ready, true, memory_order_release);
#ifndef NRF_ENGINE_HOST
    ESP_LOGI(TAG, "pipeline ready: ring=%u heap=%u largest=%u", (unsigned)s_ring_bytes,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    return true;
failed:
#ifndef NRF_ENGINE_HOST
    ESP_LOGW(TAG, "pipeline allocation failed at %s: heap=%u largest=%u", stage,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    (void)stage;
#endif
    if (s_network) {
        vTaskDelete(s_network);
        s_network = NULL;
    }
    atomic_store(&s_network_ready, false);
    atomic_store(&s_done, true);
    atomic_store(&s_active, false);
    decode_delete(s_decode);
    s_decode = NULL;
    if (s_ring) {
        ring_delete(s_ring);
        s_ring = NULL;
    }
    s_ring_bytes = 0;
    atomic_store(&s_state, FAILED);
    allocation_backoff();
    app_nabo_radio_network_end();
    return false;
}
static int decode_frame(decode_t* d, nabo_radio_frame_info_t* info) {
    if (!d->codec)
        return nabo_radio_decoder_decode(&d->mp3, d->input, (int)d->used, d->pcm, info);
    // ADTS preflight bounds decoder reads and preserves split headers/frames.
    if (d->used < 7)
        return 0;
    unsigned off = 0;
    while (off + 7 <= d->used && !(d->input[off] == 255 && (d->input[off + 1] & 0xf6) == 0xf0))
        off++;
    if (off) {
        info->frame_bytes = (int)off;
        return 0;
    }
    size_t n = ((size_t)(d->input[3] & 3) << 11) | ((size_t)d->input[4] << 3) | (d->input[5] >> 5);
    if (n < 7 || n > INPUT_BYTES) {
        info->frame_bytes = 1;
        return 0;
    }
    if (n > d->used)
        return 0;
    esp_audio_dec_in_raw_t raw = {.buffer = d->input, .len = n};
    esp_audio_dec_out_frame_t out = {.buffer = (uint8_t*)d->pcm, .len = PCM_BYTES};
    esp_audio_dec_info_t dec = {0};
    esp_audio_err_t err = esp_aac_dec_decode(d->aac, &raw, &out, &dec);
    info->frame_bytes = (int)n;
    if (err != ESP_AUDIO_ERR_OK || dec.bits_per_sample != 16 || !dec.channel || dec.channel > 2 ||
        out.decoded_size > PCM_BYTES)
        return -1;
    info->channels = dec.channel;
    info->hz = (int)dec.sample_rate;
    return (int)(out.decoded_size / (2 * dec.channel));
}
size_t nabo_radio_read(int16_t* out, size_t frames, int wait_ms) {
    (void)wait_ms;
    if (!s_decode || !out || !frames)
        return 0;
    decode_t* d = s_decode;
    if (cancelled(s_audio_generation)) {
        rebuffer_end();
        if (!s_tail)
            s_tail = nabo_radio_output_rate() * 60 / 1000;
        size_t n = frames < s_tail ? frames : s_tail;
        for (size_t i = 0; i < n; i++) {
            s_last = (int16_t)((int32_t)s_last * (int32_t)(s_tail - 1) / (int32_t)s_tail);
            out[i] = s_last;
            --s_tail;
        }
        return n;
    }
    if (!d->prefilled) {
        bool recovering = atomic_load(&s_heard_audio);
        size_t prefill = recovering ? s_ring_bytes / 4 : s_ring_bytes * 3 / 4;
        size_t limit = recovering ? REBUFFER_BYTES : PREFILL_BYTES;
        if (prefill > limit)
            prefill = limit;
        if (ring_available(s_ring) < prefill && !atomic_load(&s_done)) {
            atomic_store(&s_state, BUFFERING);
            return 0;
        }
        d->prefilled = true;
        if (recovering) {
            d->dsp.fade = 0;
            nabo_radio_dsp_fade(&d->dsp, true, 5);
        }
    }
    size_t written = 0;
    unsigned work = 0;
    while (written < frames && work++ < 32) {
        if (d->at < d->ready) {
            size_t n = d->ready - d->at;
            if (n > frames - written)
                n = frames - written;
            memcpy(out + written, d->out + d->at, n * 2);
            d->at += n;
            written += n;
            continue;
        }
        size_t got = ring_receive(s_ring, d->input + d->used, INPUT_BYTES - d->used);
        d->used += got;
        if (d->leading) {
            if (d->used < 10) {
                // An HTTP failure or truncated ID3 header can end before the
                // first frame. Let the owner reclaim RAM and retry that case
                // too; otherwise it remains active forever with zero audio.
                if (atomic_load(&s_done) && !ring_available(s_ring))
                    d->drained = true;
                break;
            }
            if (!d->id3_left && !memcmp(d->input, "ID3", 3)) {
                if ((d->input[6] | d->input[7] | d->input[8] | d->input[9]) & 128) {
                    nabo_radio_stop();
                    break;
                }
                d->id3_left = 10 + ((size_t)d->input[6] << 21) + ((size_t)d->input[7] << 14) +
                              ((size_t)d->input[8] << 7) + d->input[9];
                if (d->id3_left > 65536) {
                    nabo_radio_stop();
                    break;
                }
            }
            if (d->id3_left) {
                size_t n = d->used < d->id3_left ? d->used : d->id3_left;
                d->used -= n;
                d->id3_left -= n;
                memmove(d->input, d->input + n, d->used);
                if (d->id3_left)
                    continue;
            }
            d->leading = false;
        }
        nabo_radio_frame_info_t info = {0};
        int n = decode_frame(d, &info);
        if (n < 0 || info.frame_bytes < 0 || (size_t)info.frame_bytes > d->used) {
            nabo_radio_stop();
            atomic_store(&s_state, FAILED);
            break;
        }
        if (!info.frame_bytes) {
            if (atomic_load(&s_done) && !ring_available(s_ring))
                d->drained = true;
            else if (!ring_available(s_ring)) {
                // Keep the partial frame and Helix reservoir. Recover a useful
                // cushion after starvation instead of playing each arriving
                // packet followed by another short gap.
                d->prefilled = false;
                if (atomic_load(&s_heard_audio) || written)
                    rebuffer_begin();
                atomic_store(&s_state, BUFFERING);
            }
            if (d->used == INPUT_BYTES) {
                memmove(d->input, d->input + 1, --d->used);
            }
            break;
        }
        d->used -= (size_t)info.frame_bytes;
        memmove(d->input, d->input + info.frame_bytes, d->used);
        d->scanned += (size_t)info.frame_bytes;
        if (!n) {
            if (d->scanned > 96 * 1024) {
                nabo_radio_stop();
                atomic_store(&s_state, FAILED);
                break;
            }
            continue;
        }
        d->scanned = 0;
        if (n > 1152 || (info.channels != 1 && info.channels != 2) || info.hz < 8000 ||
            info.hz > 48000) {
            nabo_radio_stop();
            atomic_store(&s_state, FAILED);
            break;
        }
        unsigned bitrate = info.bitrate_kbps > 0
                               ? (unsigned)info.bitrate_kbps
                               : (unsigned)((uint64_t)info.frame_bytes * 8 * info.hz / n / 1000);
        if (bitrate)
            atomic_store(&s_bitrate_kbps, bitrate);
        if (d->rate != (unsigned)info.hz) {
            d->rate = (unsigned)info.hz;
            nabo_radio_dsp_init(&d->dsp, d->rate);
            nabo_radio_dsp_fade(&d->dsp, true, 40);
            atomic_store(&s_rate, (int)nabo_radio_dsp_out_rate_for(d->rate));
            remember(nabo_radio_current_station());
            ESP_LOGI(TAG, "decoded stream: rate=%d channels=%d output=%u", info.hz, info.channels,
                     nabo_radio_output_rate());
        }
        nabo_radio_dsp_set_loudness(&d->dsp, nabo_radio_loudness());
        d->ready =
            nabo_radio_dsp_feed(&d->dsp, d->pcm, (size_t)n, info.channels, d->out, OUTPUT_SAMPLES);
        d->at = 0;
        atomic_store(&s_meter, nabo_radio_dsp_meter(&d->dsp));
        atomic_store(&s_state, PLAYING);
        s_retry_delay = 1;
        s_alloc_retry_delay = 3;
    }
    unsigned remaining = nabo_radio_sleep_remaining();
    if (remaining && remaining < 60)
        for (size_t i = 0; i < written; i++)
            out[i] = (int16_t)((int32_t)out[i] * (int)remaining / 60);
    if (written) {
        atomic_store(&s_heard_audio, true);
        /* A short block may have just started the next wait. Close only a
         * wait whose recovery threshold was reached and yielded real PCM. */
        if (d->prefilled)
            rebuffer_end();
        s_last = out[written - 1];
    }
    atomic_fetch_add(&s_played, (unsigned)written);
    return written;
}
bool nabo_radio_finished(void) {
    if (!atomic_load_explicit(&s_done, memory_order_acquire))
        return false;
    if (cancelled(s_audio_generation))
        return s_last == 0;
    return !s_decode || s_decode->drained;
}
void nabo_radio_finish(void) {
    if (!atomic_load_explicit(&s_done, memory_order_acquire) || !nabo_radio_active())
        return;
    if (!cancelled(s_audio_generation) && atomic_load(&s_heard_audio))
        rebuffer_begin(); /* Keep the same station's gap timed across automatic rebuilds. */
    else
        rebuffer_end();
    if (s_network) {
        vTaskDelete(s_network);
        s_network = NULL;
    }
    atomic_store(&s_network_ready, false);
    if (s_ring) {
        ring_delete(s_ring);
        s_ring = NULL;
    }
    s_ring_bytes = 0;
    decode_delete(s_decode);
    s_decode = NULL;
    atomic_store(&s_active, false);
    atomic_store(&s_meter, 0);
    save_settings();
    app_nabo_radio_network_end();
    if (nabo_radio_wanted()) {
        atomic_store(&s_state, RETRYING);
        if (atomic_exchange(&s_release_for_memory, false))
            allocation_backoff();
        else {
            schedule_retry(atomic_load(&s_generation) != s_audio_generation ? 0
                                                                            : s_retry_delay * 1000);
            if (s_retry_delay < 8)
                s_retry_delay *= 2;
        }
    }
}
