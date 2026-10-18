#include "nabo_table.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nabo_table_scene.h"
#include "nabo_table_sound.h"
#include "nvs.h"

#define TAG "NABO-Table"
#define FRAME_US 33333          // 30 fps; NABO's sprites run at 12 fps
#define REPEAT_DELAY_MS 350     // UP / DOWN auto-repeat, as in the prototype
#define REPEAT_EVERY_MS 90
#define LONG_OK_MS 2000         // hold OK to pause

LV_FONT_DECLARE(nabo_table_font_12)
LV_FONT_DECLARE(nabo_menu_font_16)
LV_FONT_DECLARE(nabo_ui_font_18)
LV_FONT_DECLARE(nabo_table_font_24)
LV_FONT_DECLARE(nabo_mono_font_12)
LV_FONT_DECLARE(nabo_poster_font_28)

typedef struct {
    nt_t model;
    nt_sound_t sound;
    nts_pack_t pack;
} table_state_t;

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static table_state_t *s_st;
static ntg_t *s_gfx;
static uint16_t *s_strip[2];
static uint8_t *s_sprite_buf;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static esp_timer_handle_t s_timer;
static atomic_bool s_stop, s_running, s_capture, s_capturing;
static uint32_t s_seed;

// Input: button edges and injected keys, from other tasks.
#define EDGE_RING 32
typedef struct { uint8_t button, pressed, injected; uint32_t ms; } edge_t;
static edge_t s_edges[EDGE_RING];
static atomic_uint s_edge_head, s_edge_tail;
static atomic_uint s_events;   // sound events for the audio worker

static void push_edge(edge_t e)
{
    unsigned head = atomic_load(&s_edge_head);
    if (head - atomic_load(&s_edge_tail) >= EDGE_RING) return;
    s_edges[head % EDGE_RING] = e;
    atomic_store(&s_edge_head, head + 1);
}

void nabo_table_button(int button, bool pressed, uint32_t event_ms)
{
    if (!atomic_load(&s_running) || button < 0 || button > 2) return;
    push_edge((edge_t){(uint8_t)button, pressed, 0, event_ms});
}

void nabo_table_inject(int key)
{
    if (atomic_load(&s_running) && key >= 0 && key <= 3) push_edge((edge_t){(uint8_t)key, 1, 1, 0});
}

void nabo_table_audio_fill(int16_t *out, size_t count)
{
    table_state_t *st = s_st;
    if (!atomic_load(&s_running) || !st) return;
    uint32_t e = atomic_exchange(&s_events, 0);
    if (e) nt_sound_events(&st->sound, e);
    nt_sound_fill(&st->sound, out, count);
}

int nabo_table_status(char *out, size_t cap)
{
    table_state_t *st = s_st;
    if (!atomic_load(&s_running) || !st) return snprintf(out, cap, "{\"running\":0}");
    const nt_t *m = &st->model;
    return snprintf(out, cap, "{\"running\":1,\"scene\":%d,\"game\":%d,\"score\":[%d,%d],\"round\":%d,\"turn\":%d,"
                              "\"paused\":%d,\"heap\":%u}",
                    m->scene, m->game, m->score[0], m->score[1], m->round, m->turn, m->paused,
                    (unsigned)esp_get_free_heap_size());
}

/* ---------- settings ---------- */

static uint8_t load_target(void)
{
    nvs_handle_t h;
    uint8_t v = 3;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "table_target", &v);
        nvs_close(h);
    }
    return v == 3 || v == 5 || v == 7 ? v : 3;
}

static void save_target(uint8_t v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "table_target", v);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "target save failed: %d", err);
}

/* ---------- game task ---------- */

static void send(void *ctx, int x, int y, int w, int h, uint16_t *px)
{
    (void)ctx;
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, px);
    if (atomic_load(&s_capturing)) {   // serial screenshot: the strip exactly as sent (big-endian RGB565)
        static const char HEX[] = "0123456789abcdef";
        char line[NTG_W * 4 + 1];
        for (int r = 0; r < h; r++) {
            const uint8_t *b = (const uint8_t *)(px + r * w);
            for (int i = 0; i < w * 2; i++) { line[2 * i] = HEX[b[i] >> 4]; line[2 * i + 1] = HEX[b[i] & 15]; }
            line[w * 4] = 0;
            printf("@cap %d %d %d %s\n", y + r, x, w, line);
        }
    }
}

void nabo_table_capture(void) { atomic_store(&s_capture, true); }

static void tick(void *arg)
{
    (void)arg;
    if (s_task) xTaskNotifyGive(s_task);
}

typedef struct {
    bool down;
    uint32_t since, next_repeat;
    bool long_sent;
} key_state_t;

// Turns edges into model keys: UP / DOWN act on press and repeat while held; OK acts on release,
// or once as a long press after LONG_OK_MS.
static void handle_input(nt_t *m, key_state_t keys[3], uint32_t now_ms)
{
    while (atomic_load(&s_edge_tail) != atomic_load(&s_edge_head)) {
        unsigned tail = atomic_load(&s_edge_tail);
        edge_t e = s_edges[tail % EDGE_RING];
        atomic_store(&s_edge_tail, tail + 1);
        if (e.injected) {
            nt_key(m, e.button == 3 ? NT_KEY_OK_LONG : (nt_key_t)e.button);
            continue;
        }
        key_state_t *k = &keys[e.button];
        if (e.pressed && !k->down) {
            k->down = true;
            k->since = e.ms;
            k->long_sent = false;
            k->next_repeat = e.ms + REPEAT_DELAY_MS;
            if (e.button != NABO_TABLE_OK) nt_key(m, e.button == NABO_TABLE_UP ? NT_KEY_UP : NT_KEY_DOWN);
        } else if (!e.pressed && k->down) {
            k->down = false;
            if (e.button == NABO_TABLE_OK && !k->long_sent) nt_key(m, NT_KEY_OK);
        }
    }
    for (int b = 0; b < 2; b++)
        while (keys[b].down && (int32_t)(now_ms - keys[b].next_repeat) >= 0) {
            keys[b].next_repeat += REPEAT_EVERY_MS;
            nt_key(m, b == NABO_TABLE_UP ? NT_KEY_UP : NT_KEY_DOWN);
        }
    key_state_t *ok = &keys[NABO_TABLE_OK];
    if (ok->down && !ok->long_sent && now_ms - ok->since >= LONG_OK_MS && nt_can_pause(m)) {
        ok->long_sent = true;
        nt_key(m, NT_KEY_OK_LONG);
    }
}

static void game_task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait until nabo_table_open() finished allocating
    if (atomic_load(&s_stop)) {
        xSemaphoreGive(s_done);
        vTaskDelete(NULL);
        return;
    }
    nt_fonts_t fonts = {&nabo_table_font_12, &nabo_menu_font_16, &nabo_ui_font_18,
                        &nabo_table_font_24, &nabo_mono_font_12, &nabo_poster_font_28};
    nt_config_t cfg = {.target = load_target(), .dice_per = 5, .min_open = 2, .wild = true};
    // One-shot lengths come from the sprites, so scene timing matches what NABO is doing.
    static const struct { nt_anim_t anim; int sprite; } LEN[] = {
        {NT_ANIM_SHUFFLE, NTS_SMALL_CUT}, {NT_ANIM_DEAL, NTS_SMALL_DEAL}, {NT_ANIM_SLAM, NTS_SMALL_SLAM},
        {NT_ANIM_COVER_IN, NTS_LARGE_COVER_IN}, {NT_ANIM_OPEN, NTS_SMALL_OPEN}, {NT_ANIM_SURPRISE, NTS_SMALL_SURPRISE}};
    for (size_t i = 0; i < sizeof(LEN) / sizeof(LEN[0]); i++) {
        nts_anim_t a;
        if (nts_anim(&s_st->pack, LEN[i].sprite, &a) && a.fps) cfg.anim_s[LEN[i].anim] = (float)a.frames / a.fps;
    }
    nt_t *m = &s_st->model;
    nt_init(m, &cfg, s_seed);
    nt_sound_reset(&s_st->sound);
    key_state_t keys[3] = {0};
    int64_t last = esp_timer_get_time(), stats_at = last;
    uint32_t frames = 0, work_us = 0, max_us = 0, bytes = 0;
    while (!atomic_load(&s_stop)) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        int64_t now = esp_timer_get_time();
        double dt = (now - last) / 1e6;
        last = now;
        if (dt > 0.1) dt = 0.1;
        handle_input(m, keys, (uint32_t)(now / 1000));
        nt_update(m, dt);
        uint32_t ev = nt_take_events(m);
        if (ev & NT_EV_SETTINGS) save_target(m->cfg.target);
        if (ev) atomic_fetch_or(&s_events, ev);
        bool capture = atomic_exchange(&s_capture, false);
        if (capture) {
            ntg_invalidate(s_gfx);
            atomic_store(&s_capturing, true);
            printf("\n@cap begin\n");
        }
        int64_t t0 = esp_timer_get_time();
        nt_scene_build(s_gfx, m, &fonts);
        ntg_flush(s_gfx);
        if (capture) {
            atomic_store(&s_capturing, false);
            printf("@cap end\n");
        }
        uint32_t used = (uint32_t)(esp_timer_get_time() - t0);
        ++frames, work_us += used, bytes += s_gfx->bytes;
        if (used > max_us) max_us = used;
        if (now - stats_at >= 5000000) {
            ESP_LOGI(TAG, "scene=%d fps=%lu frame_us mean=%lu max=%lu bytes=%lu ops=%d glyphs=%d heap=%u",
                     (int)m->scene, (unsigned long)(frames * 1000000ULL / (now - stats_at)),
                     (unsigned long)(work_us / frames), (unsigned long)max_us, (unsigned long)(bytes / frames),
                     s_gfx->n, s_gfx->glyphs.used, (unsigned)esp_get_free_heap_size());
            stats_at = now, frames = work_us = max_us = bytes = 0;
        }
    }
    esp_lcd_panel_io_tx_param(s_io, 0x00, NULL, 0);   // NOP: returns once the last strip has gone out
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

/* ---------- lifecycle ---------- */

static void release(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    heap_caps_free(s_gfx);
    heap_caps_free(s_strip[0]);
    heap_caps_free(s_strip[1]);
    heap_caps_free(s_sprite_buf);
    heap_caps_free(s_st);
    s_gfx = NULL, s_strip[0] = s_strip[1] = NULL, s_sprite_buf = NULL, s_st = NULL;
}

static void log_heap(const char *what)
{
    ESP_LOGI(TAG, "%s: free=%u largest=%u", what, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static void *alloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static void *alloc_dma(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL); }

bool nabo_table_open(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, uint32_t seed, const void *pack,
                     size_t pack_size)
{
    if (atomic_load(&s_running)) return true;
    s_io = io, s_panel = panel, s_seed = seed;
    log_heap("open");
    if (!s_done) s_done = xSemaphoreCreateBinary();
    atomic_store(&s_stop, false);
    atomic_store(&s_events, 0);
    atomic_store(&s_edge_tail, atomic_load(&s_edge_head));
    s_task = NULL;
    // Largest blocks first while the heap still has big holes, then the task stack.
    s_gfx = alloc(sizeof(ntg_t));
    s_st = alloc(sizeof(table_state_t));
    bool ok = s_gfx && s_st && nts_open(&s_st->pack, pack, pack_size);
    size_t need = ok ? nts_buffer_need(&s_st->pack) : 0;
    if (ok) {
        s_sprite_buf = alloc(need);
        s_strip[0] = alloc_dma(NTG_W * NTG_STRIP * 2);
        s_strip[1] = alloc_dma(NTG_W * NTG_STRIP * 2);
        ok = s_sprite_buf && s_strip[0] && s_strip[1];
    }
    if (!ok || !s_done || xTaskCreate(game_task, "nabo_table", 6144, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
        log_heap(ok ? "open: task failed" : "open: buffers or sprite pack failed");
        release();
        return false;
    }
    ntg_init(s_gfx, &s_st->pack, s_sprite_buf, need, s_strip[0], s_strip[1], send, NULL);
    const esp_timer_create_args_t args = {.callback = tick, .name = "nabo_table"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        atomic_store(&s_stop, true);
        xTaskNotifyGive(s_task);   // the task sees s_stop before touching any buffer
        xSemaphoreTake(s_done, pdMS_TO_TICKS(500));
        s_task = NULL;
        release();
        return false;
    }
    atomic_store(&s_running, true);
    xTaskNotifyGive(s_task);
    esp_timer_start_periodic(s_timer, FRAME_US);
    log_heap("open: running");
    return true;
}

void nabo_table_close(void)
{
    if (!atomic_load(&s_running)) return;
    atomic_store(&s_stop, true);
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) ESP_LOGE(TAG, "close: game task did not stop");
    s_task = NULL;
    atomic_store(&s_running, false);   // the audio worker stops touching s_st first
    vTaskDelay(pdMS_TO_TICKS(40));      // let a fill already in progress (20 ms blocks) finish
    release();
    log_heap("closed");
}

bool nabo_table_running(void) { return atomic_load(&s_running); }

bool nabo_table_on_cover(void)
{
    table_state_t *st = s_st;
    return !atomic_load(&s_running) || !st || (st->model.scene == NT_MENU && !st->model.paused);
}
