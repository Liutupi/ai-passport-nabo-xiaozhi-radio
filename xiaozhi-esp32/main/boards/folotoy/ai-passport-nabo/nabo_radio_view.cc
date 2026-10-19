#include "nabo_radio_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#ifdef NRF_HOST_PREVIEW
#include "radio_host_platform.h"
#else
#include "assets.h"
#include "backlight.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nabo_apps.h"
#include "nabo_display.h"
#endif
extern "C" {
#include "muse_wifi.h"
#include "nabo_radio.h"
#include "nabo_radio_gfx.h"
#include "nabo_radio_keys.h"
}

namespace {
enum Page { PLAYER, MENU, LIST, SEARCH, SLEEP, LOCAL };
constexpr uint16_t BG = 0x0842, TEXT = 0xf77c, MUTED = 0x8c94, DIM = 0x526c, AMBER = 0xf5ca,
                   RED = 0xf9c6, LEVEL = 0x6718, DESK = 0x1926, ROW = 0x2129, DIAL = 0x1080,
                   TICK = 0x6a84;
constexpr int SLEEPS[] = {0, 15, 30, 60, 90};
const char* const MENU_ITEMS[] = {"电台列表", "拼音搜台", "定时关闭", "换台沙沙声",
                                  "响度均衡", "本地地区", "设置",     "返回"};
const char* const ACTIONS[] = {"idle_off", "phones_on", "phones_off", "tune", "groove",
                               "listen",   "sip",       "love",       "oops", "doze",
                               "wave",     "search",    "sing"};
struct View {
    nrf_gfx_t gfx;
    nrf_sprite_t sprite;
    nrf_keys_t keys;
    nrf_font_t name, ui, logo, clockfont, led;
    uint8_t pixels[158 * 139];
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    uint16_t* strip;
    const void *mic, *head;
    uint32_t hashes[40];
    int count[80], start[80], bands, total, band = 0, position = 0, current = 0, selection = 0,
                                            top = 0;
    Page page = PLAYER;
    int undo_band = 0, undo_position = 0, undo_selection = 0, undo_current = 0;
    int player_band = 0, player_position = 0;
    Page undo_page = PLAYER;
    bool undo = false, clock = false, swallow[3] = {}, refresh = true;
    bool dimmed = false;
    int brightness = 80;
    int action = -1, last_state = -1, level = 0;
    uint32_t action_at = 0, last_key = 0, connect_at = 0, sip_at = 0, dial_at = 0, toast_until = 0;
    int dial_from = 0, dial_to = 0;
    char query[7] = {}, message[96] = {};
    int hits[12], hit_count = 0;
};
View* v;
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void toast(const char* text) {
    snprintf(v->message, sizeof(v->message), "%s", text);
    v->toast_until = now_ms() + 1800;
}
int band_station(int b, int pos) {
    if (b == 2)
        return nabo_radio_recent(pos);
    for (int i = 0; i < nabo_radio_station_count(); i++) {
        nrf_station_t s;
        nabo_radio_station_info(i, &s);
        bool match = b == 0   ? (nabo_radio_local() < 0 ||
                                 s.region_id == static_cast<unsigned>(nabo_radio_local()))
                     : b == 1 ? nabo_radio_favorite(i)
                     : b < 14 ? (s.types & (1u << (b - 3))) != 0
                              : s.region_id == static_cast<unsigned>(b - 14);
        if (match && pos-- == 0)
            return i;
    }
    return -1;
}
const char* band_name(int b) {
    if (b == 0) {
        const char* s = nabo_radio_category_name(nabo_radio_local());
        return s && *s ? s : "全国";
    }
    if (b == 1)
        return "收藏";
    if (b == 2)
        return "最近";
    return b < 14 ? nrf_catalog_type(b - 3) : nabo_radio_category_name(b - 14);
}
void rebuild() {
    memset(v->count, 0, sizeof(v->count));
    v->bands = std::min(80, 14 + nabo_radio_category_count());
    for (int i = 0; i < nabo_radio_station_count(); i++) {
        nrf_station_t s;
        nabo_radio_station_info(i, &s);
        if (nabo_radio_local() < 0 || s.region_id == static_cast<unsigned>(nabo_radio_local()))
            v->count[0]++;
        if (nabo_radio_favorite(i))
            v->count[1]++;
        if (s.region_id + 14 < static_cast<unsigned>(v->bands))
            v->count[s.region_id + 14]++;
        for (int t = 0; t < NRF_TYPES; t++)
            if (s.types & (1u << t))
                v->count[t + 3]++;
    }
    for (int i = 0; i < 10; i++)
        if (nabo_radio_recent(i) >= 0)
            v->count[2]++;
    v->total = 0;
    for (int b = 0; b < v->bands; b++) {
        v->start[b] = v->total;
        v->total += v->count[b];
    }
    if (!v->count[v->band]) {
        v->band = 0;
        v->position = 0;
    }
    v->position = std::clamp(v->position, 0, std::max(0, v->count[v->band] - 1));
}
int station_at(int flat, int* band = nullptr) {
    if (!v->total)
        return -1;
    flat = (flat % v->total + v->total) % v->total;
    for (int b = 0; b < v->bands; b++)
        if (flat >= v->start[b] && flat < v->start[b] + v->count[b]) {
            if (band)
                *band = b;
            return band_station(b, flat - v->start[b]);
        }
    return -1;
}
void action(int id) {
    if (v->action == id)
        return;
    if (nrf_sprite_select(&v->sprite, ACTIONS[id])) {
        v->action = id;
        v->action_at = now_ms();
    }
}
int base_action() {
    if (!nabo_radio_wanted() || nabo_radio_state() == 6)
        return 0;
    if (nabo_radio_state() == 4)
        return 11;
    if (nabo_radio_state() == 3) {
        nrf_station_t s = {};
        nabo_radio_station_info(v->current, &s);
        return s.types & ((1u << 1) | (1u << 4)) ? 4 : 5;
    }
    return 3;
}
void move(int direction) {
    if (v->page == PLAYER) {
        int flat = v->start[v->band] + v->position;
        int dest = (flat + direction + v->total) % std::max(v->total, 1), b = v->band;
        int id = station_at(dest, &b);
        if (id < 0)
            return;
        v->dial_from = flat;
        v->dial_to = dest;
        v->dial_at = now_ms();
        if (abs(dest - flat) > v->total / 2)
            v->dial_from = dest - direction;
        if (b != v->band)
            toast(band_name(b));
        v->band = b;
        v->position = dest - v->start[b];
        v->current = id;
        v->connect_at = now_ms() + 500;
        if (nabo_radio_wanted())
            action(3);
    } else {
        int n = v->page == MENU     ? 8
                : v->page == LIST   ? v->count[v->band]
                : v->page == SEARCH ? 28 + v->hit_count
                : v->page == SLEEP  ? 5
                                    : nabo_radio_category_count() + 1;
        if (n)
            v->selection = (v->selection + direction + n) % n;
    }
}
void favorite(int id) {
    if (id < 0)
        return;
    bool was = nabo_radio_favorite(id);
    nabo_radio_toggle_favorite(id);
    rebuild();
    toast(nabo_radio_favorite(id) ? "已收藏" : was ? "已取消收藏" : "收藏已满 128 台");
    action(7);
}
void choose() {
    if (v->page == PLAYER) {
        nabo_radio_toggle_play();
        if (nabo_radio_wanted()) {
            nabo_radio_select_station(v->current);
            action(1);
        } else
            action(2);
        v->connect_at = 0;
        return;
    }
    if (v->page == MENU) {
        int s = v->selection;
        v->selection = 0;
        v->top = 0;
        if (s == 0) {
            v->player_band = v->band;
            v->player_position = v->position;
            v->page = LIST;
        }
        if (s == 1) {
            v->page = SEARCH;
            v->query[0] = 0;
            v->hit_count = 0;
        }
        if (s == 2)
            v->page = SLEEP;
        if (s == 3) {
            nabo_radio_set_noise(!nabo_radio_noise());
            toast(nabo_radio_noise() ? "换台沙沙声已开启" : "换台沙沙声已关闭");
            v->page = PLAYER;
        }
        if (s == 4) {
            nabo_radio_set_loudness(!nabo_radio_loudness());
            toast(nabo_radio_loudness() ? "响度均衡已开启" : "响度均衡已关闭");
            v->page = PLAYER;
        }
        if (s == 5) {
            v->page = LOCAL;
            v->selection = nabo_radio_local() + 1;
        }
        if (s == 6)
            nabo_apps_open(NABO_SETTINGS);
        if (s == 7)
            v->page = PLAYER;
        return;
    }
    if (v->page == LIST || v->page == SEARCH) {
        int id = -1;
        if (v->page == SEARCH && v->selection < 28) {
            size_t n = strlen(v->query);
            if (v->selection == 26) {
                if (n)
                    v->query[n - 1] = 0;
            } else if (v->selection == 27) {
                if (v->hit_count)
                    v->selection = 28;
                return;
            } else if (n < 6) {
                v->query[n] = static_cast<char>('A' + v->selection);
                v->query[n + 1] = 0;
            }
            v->hit_count = 0;
            for (int i = 0; i < nabo_radio_station_count() && v->hit_count < 12; i++) {
                nrf_station_t s;
                nabo_radio_station_info(i, &s);
                if (nrf_catalog_match(&s, v->query))
                    v->hits[v->hit_count++] = i;
            }
            return;
        }
        id = v->page == LIST ? band_station(v->band, v->selection) : v->hits[v->selection - 28];
        if (id >= 0) {
            v->current = id;
            nabo_radio_select_station(id);
            v->connect_at = 0;
            v->page = PLAYER;
            action(1);
            v->position = 0;
            for (int i = 0; i < v->count[v->band]; i++)
                if (band_station(v->band, i) == id) {
                    v->position = i;
                    break;
                }
        }
        return;
    }
    if (v->page == SLEEP) {
        nabo_radio_set_sleep(SLEEPS[v->selection]);
        toast(v->selection ? "定时关闭已设置" : "已取消定时");
        v->page = PLAYER;
    }
    if (v->page == LOCAL) {
        nabo_radio_set_local(v->selection - 1);
        rebuild();
        v->page = PLAYER;
        toast("本地地区已保存");
    }
}
void key_event(void*, int key, int event) {
    if (key == NRF_UP && event == NRF_LONG) {
        nabo_apps_open(NABO_HOME);
        return;
    }
    if (key == NRF_UP && event == NRF_DOUBLE) {
        if (v->undo) {
            v->band = v->undo_band;
            v->position = v->undo_position;
            v->selection = v->undo_selection;
            v->page = v->undo_page;
            v->current = v->undo_current;
            v->connect_at = 0;
            v->undo = false;
        }
        if (!nabo_radio_view_back())
            nabo_apps_open(NABO_TOOLS);
        return;
    }
    if (key == NRF_OK && event == NRF_DOUBLE) {
        favorite(v->page == LIST ? band_station(v->band, v->selection) : v->current);
        return;
    }
    if (key == NRF_OK && event == NRF_LONG) {
        if (v->page == LIST) {
            do {
                v->band = (v->band + 1) % v->bands;
            } while (!v->count[v->band]);
            v->selection = 0;
            v->top = 0;
        } else {
            v->page = v->page == PLAYER ? MENU : PLAYER;
            v->selection = 0;
        }
        return;
    }
    if (key == NRF_OK && event == NRF_CLICK) {
        choose();
        return;
    }
    if (key == NRF_UP && event == NRF_CLICK) {
        v->undo = true;
        v->undo_page = v->page;
        v->undo_band = v->band;
        v->undo_position = v->position;
        v->undo_selection = v->selection;
        v->undo_current = v->current;
    }
    if (key == NRF_UP || key == NRF_DOWN)
        move(key == NRF_UP ? -1 : 1);
}
bool asset(const char* name, const void*& data, size_t& size) {
    void* p = nullptr;
    bool ok = Assets::GetInstance().GetAssetData(name, p, size);
    data = p;
    return ok;
}
void text(const char* s, int x, int y, int width = 224, int align = -1, uint16_t color = TEXT,
          const nrf_font_t* font = nullptr) {
    nrf_text(&v->gfx, font ? font : &v->ui, s, x, y, width, align, color);
}
void rect(int x, int y, int w, int h, uint16_t c) { nrf_rect(&v->gfx, x, y, w, h, c); }
void status(int battery) {
    rect(0, 0, 240, 18, BG);
    text("NABO FM", 6, 14, 100, -1, AMBER, &v->logo);
    rect(146, 4, 38, 11, nabo_radio_state() == 3 ? RED : 0x30e3);
    text("ON AIR", 165, 13, 38, 0, TEXT, &v->led);
    if (muse_wifi_connected()) {
        nrf_line(&v->gfx, 191, 7, 195, 4, MUTED);
        nrf_line(&v->gfx, 195, 4, 200, 7, MUTED);
        nrf_line(&v->gfx, 193, 9, 195, 7, MUTED);
        nrf_line(&v->gfx, 195, 7, 198, 9, MUTED);
        rect(195, 11, 2, 2, MUTED);
    } else
        text("-", 191, 14, 12, -1, MUTED);
    rect(214, 5, 18, 8, MUTED);
    rect(232, 7, 2, 4, MUTED);
    rect(215, 6, 16, 6, BG);
    if (battery >= 0)
        rect(216, 7, std::clamp(battery, 0, 100) * 14 / 100, 4, LEVEL);
}
void stage(uint32_t now) {
    nrf_stage(&v->gfx);
    nrf_sprite(&v->gfx, &v->sprite, 41, 11);
    nrf_line(&v->gfx, 236, 28, 203, 100, 0x526c);
    nrf_line(&v->gfx, 237, 28, 204, 100, 0x526c);
    nrf_prop(&v->gfx, v->mic, 190, 92);
    if (v->action == 9)
        text("Zzz", 172, 75, 40, -1, MUTED);
    if (v->action == 7)
        text("♥", 169, 64, 30, 0, RED, &v->name);
    rect(0, 150, 240, 28, DESK);
    rect(0, 150, 240, 2, 0x29a9);
    for (int i = 0; i < 12; i++) {
        int h = v->level * (6 + (i * 7) % 5) / 256;
        for (int j = 0; j < 5; j++)
            rect(10 + i * 7, 172 - j * 4, 5, 3,
                 j < h ? (j == 4   ? RED
                          : j == 3 ? AMBER
                                   : LEVEL)
                       : 0x2129);
    }
    text(nabo_radio_favorite(v->current) ? "♥" : "♡", 186, 170, 20, 0,
         nabo_radio_favorite(v->current) ? RED : DIM);
    nrf_circle(&v->gfx, 214, 164, 10, AMBER);
    nrf_circle(&v->gfx, 214, 164, 8, 0x394b);
    float angle = static_cast<float>(v->dial_to) * 0.9f;
    nrf_line(&v->gfx, 214, 164, 214 + static_cast<int>(cosf(angle) * 7),
             164 + static_cast<int>(sinf(angle) * 7), AMBER);
    (void)now;
}
void info() {
    text(nabo_radio_station_name(v->current), 120, 203, 224, 0, TEXT, &v->name);
    nrf_station_t s;
    nabo_radio_station_info(v->current, &s);
    char sub[128];
    snprintf(sub, sizeof(sub), "%s · %s", s.region,
             v->connect_at ? "调台中…" : nabo_radio_state_text());
    text(sub, 120, 217, 224, 0, MUTED);
}
void dial(uint32_t now) {
    rect(6, 224, 228, 56, DIAL);
    rect(6, 224, 228, 1, TICK);
    rect(6, 279, 228, 1, TICK);
    float t = std::min(1.0f, (now - v->dial_at) / 180.0f);
    float e = 1 - (1 - t) * (1 - t) * (1 - t);
    float pos = v->dial_from + (v->dial_to - v->dial_from) * e;
    if (!v->dial_at)
        pos = static_cast<float>(v->start[v->band] + v->position);
    int base = static_cast<int>(floorf(pos));
    for (int i = base - 7; i <= base + 7; i++) {
        int x = 120 + static_cast<int>((i - pos) * 18);
        if (x < 10 || x > 230)
            continue;
        rect(x, 246, 1, 14, AMBER);
        rect(x + 6, 252, 1, 8, TICK);
        rect(x + 12, 252, 1, 8, TICK);
        int b = 0, id = station_at(i, &b);
        nrf_circle(&v->gfx, x, 265, 2, AMBER);
        if (id >= 0 && nabo_radio_favorite(id))
            text("♥", x, 244, 16, 0, RED);
    }
    text(band_name(v->band), 12, 237, 155, -1, TICK);
    char counter[24];
    snprintf(counter, sizeof(counter), "%d/%d", v->position + 1, v->count[v->band]);
    text(counter, 228, 238, 72, 1, AMBER);
    rect(119, 226, 2, 52, RED);
}
void hints(uint32_t now) {
    rect(0, 282, 240, 38, BG);
    if (static_cast<int32_t>(v->toast_until - now) > 0) {
        rect(16, 288, 208, 24, ROW);
        text(v->message, 120, 305, 196, 0);
        return;
    }
    text(v->page == PLAYER   ? "▲▼ 换台   OK 播放/暂停"
         : v->page == SEARCH ? "▲▼ 选字母   OK 输入"
                             : "▲▼ 选择   OK 确定",
         120, 298, 228, 0, MUTED);
    text(v->page == PLAYER ? "双击OK 收藏   长按OK 菜单"
         : v->page == LIST ? "双击OK 收藏   长按OK 换波段"
                           : "双击▲ 返回",
         120, 313, 228, 0, DIM);
}
void overlay() {
    rect(0, 18, 240, 264, BG);
    if (v->page == SEARCH) {
        text("拼音首字母搜台", 12, 36, 216, -1, AMBER);
        rect(12, 42, 216, 22, ROW);
        text(*v->query ? v->query : "例如 GZJT", 20, 58, 200, -1, *v->query ? TEXT : DIM);
        for (int i = 0; i < 28; i++) {
            int x = 14 + i % 9 * 24, y = 72 + i / 9 * 24;
            bool on = v->selection == i;
            rect(x, y, 20, 20, on ? AMBER : ROW);
            char s[4] = {static_cast<char>('A' + i), 0};
            if (i == 26)
                snprintf(s, sizeof(s), "<");
            if (i == 27)
                snprintf(s, sizeof(s), "OK");
            text(s, x + 10, y + 15, 22, 0, on ? BG : TEXT);
        }
        int first = v->selection >= 28 ? std::max(0, v->selection - 32) : 0;
        for (int i = first; i < std::min(first + 5, v->hit_count); i++) {
            int y = 177 + (i - first) * 23;
            if (v->selection == 28 + i)
                rect(8, y - 17, 224, 22, ROW);
            text(nabo_radio_station_name(v->hits[i]), 16, y, 204);
        }
        if (*v->query && !v->hit_count)
            text("没有匹配的电台", 120, 200, 216, 0, MUTED);
        return;
    }
    const char* title = v->page == MENU    ? "菜单"
                        : v->page == LIST  ? band_name(v->band)
                        : v->page == SLEEP ? "定时关闭"
                                           : "本地地区";
    nrf_prop(&v->gfx, v->head, 8, 21);
    text(title, 40, 38, 190, -1, AMBER);
    int count = v->page == MENU    ? 8
                : v->page == LIST  ? v->count[v->band]
                : v->page == SLEEP ? 5
                                   : nabo_radio_category_count() + 1;
    if (v->selection < v->top)
        v->top = v->selection;
    if (v->selection > v->top + 6)
        v->top = v->selection - 6;
    for (int row = 0; row < 7 && v->top + row < count; row++) {
        int item = v->top + row, y = 50 + row * 32;
        bool on = item == v->selection;
        if (on)
            rect(6, y, 228, 30, ROW);
        char label[96], value[32] = "";
        if (v->page == LIST) {
            int id = band_station(v->band, item);
            nrf_station_t s;
            if (!nabo_radio_station_info(id, &s))
                continue;
            text(s.name, 26, y + 14, 176, -1, on ? TEXT : MUTED);
            text(s.region, 26, y + 27, 160, -1, DIM);
            if (id == nabo_radio_current_station() && nabo_radio_wanted())
                text(">", 16, y + 20, 10, 0, LEVEL);
            if (nabo_radio_favorite(id))
                text("♥", 222, y + 19, 16, 1, RED);
            continue;
        }
        if (v->page == MENU) {
            snprintf(label, sizeof(label), "%s", MENU_ITEMS[item]);
            if (item == 3 || item == 4)
                snprintf(value, sizeof(value), "%s",
                         (item == 3 ? nabo_radio_noise() : nabo_radio_loudness()) ? "开" : "关");
        } else if (v->page == SLEEP) {
            if (SLEEPS[item])
                snprintf(label, sizeof(label), "%d 分钟", SLEEPS[item]);
            else
                snprintf(label, sizeof(label), "不定时");
        } else
            snprintf(label, sizeof(label), "%s",
                     item ? nabo_radio_category_name(item - 1) : "全国（未定位）");
        text(label, 20, y + 20, 184, -1, on ? TEXT : MUTED);
        text(value, 218, y + 20, 30, 1, AMBER);
    }
    if (!count)
        text("这里还没有电台", 120, 150, 220, 0, MUTED);
    char n[32];
    snprintf(n, sizeof(n), "%d/%d", count ? v->selection + 1 : 0, count);
    text(n, 232, 278, 80, 1, MUTED);
}
void clock_page() {
    rect(0, 0, 240, 320, BG);
    time_t t = time(nullptr);
    tm local = {};
    localtime_r(&t, &local);
    char time[24];
    if (local.tm_year >= 124)
        snprintf(time, sizeof(time), "%02d:%02d", local.tm_hour, local.tm_min);
    else
        snprintf(time, sizeof(time), "--:--");
    text(time, 120, 150, 230, 0, AMBER, &v->clockfont);
    nrf_prop(&v->gfx, v->head, 108, 183);
    text(nabo_radio_station_name(v->current), 120, 235, 220, 0, MUTED);
    unsigned remaining = nabo_radio_sleep_remaining();
    if (remaining) {
        char s[48];
        snprintf(s, sizeof(s), "定时剩余 %u 分钟", (remaining + 59) / 60);
        text(s, 120, 260, 220, 0, DIM);
    }
    text("按任意键唤醒", 120, 300, 220, 0, DIM);
}
}  // namespace

bool nabo_radio_view_open() {
    if (v) {
        static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())
            ->BeginDirectDraw(&v->io, &v->panel);
        v->clock = false;
        v->last_key = now_ms();
        v->refresh = true;
        return true;
    }
    // IDF builds without exceptions can abort inside libstdc++'s nothrow new.
    // Check malloc directly before placement construction on the C3.
    void* storage = std::malloc(sizeof(View));
    if (!storage)
        return false;
    v = new (storage) View();
    const void* data;
    size_t size;
    if (asset("radio_catalog.bin", data, size))
        nabo_radio_bind_catalog(data, size);
    if (!asset("radio_sprites.bin", data, size) ||
        !nrf_sprite_init(&v->sprite, data, size, v->pixels, sizeof(v->pixels)))
        goto fail;
    if (!asset("radio_name.bin", data, size) || !nrf_font_init(&v->name, data, size))
        goto fail;
    if (!asset("radio_ui.bin", data, size) || !nrf_font_init(&v->ui, data, size))
        goto fail;
    if (!asset("radio_logo.bin", data, size) || !nrf_font_init(&v->logo, data, size))
        goto fail;
    if (!asset("radio_clock.bin", data, size) || !nrf_font_init(&v->clockfont, data, size))
        goto fail;
    if (!asset("radio_led.bin", data, size) || !nrf_font_init(&v->led, data, size))
        goto fail;
    if (asset("radio_mic.bin", data, size))
        v->mic = data;
    if (asset("radio_nabo_head_24.bin", data, size))
        v->head = data;
    v->strip = static_cast<uint16_t*>(
        heap_caps_malloc(NRF_W * NRF_STRIP * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!v->strip)
        goto fail;
    static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())
        ->BeginDirectDraw(&v->io, &v->panel);
    v->brightness = Board::GetInstance().GetBacklight()->brightness();
    nrf_keys_init(&v->keys, key_event, nullptr);
    v->last_key = now_ms();
    v->sip_at = now_ms() + 60000 + esp_random() % 60000;
    rebuild();
    v->current = nabo_radio_current_station();
    for (int i = 0; i < v->count[0]; i++)
        if (band_station(0, i) == v->current) {
            v->position = i;
            break;
        }
    v->last_state = static_cast<int>(nabo_radio_state());
    action(10);
    return true;
fail:
    if (v->strip)
        heap_caps_free(v->strip);
    v->~View();
    std::free(v);
    v = nullptr;
    ESP_LOGE("NABO-FM", "radio assets unavailable or out of memory");
    return false;
}
void nabo_radio_view_hide() {
    if (!v)
        return;
    esp_lcd_panel_io_tx_param(v->io, 0, nullptr, 0);
    if (v->dimmed) {
        Board::GetInstance().GetBacklight()->SetBrightness(v->brightness, false);
        v->dimmed = false;
    }
    static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->EndDirectDraw();
}
void nabo_radio_view_close() {
    if (!v)
        return;
    nabo_radio_view_hide();
    heap_caps_free(v->strip);
    v->~View();
    std::free(v);
    v = nullptr;
}
void nabo_radio_view_edge(int key, bool pressed, uint32_t when) {
    if (!v || key < 0 || key > 2)
        return;
    v->last_key = now_ms();
    if (v->clock && pressed) {
        v->clock = false;
        v->swallow[key] = true;
        nrf_keys_init(&v->keys, key_event, nullptr);
        v->refresh = true;
        return;
    }
    if (v->swallow[key]) {
        if (!pressed)
            v->swallow[key] = false;
        return;
    }
    nrf_keys_edge(&v->keys, key, pressed, when);
}
bool nabo_radio_view_back() {
    if (!v || v->page == PLAYER)
        return false;
    if (v->page == LIST) {
        v->band = v->player_band;
        v->position = v->player_position;
    }
    v->page = v->page == SEARCH ? LIST : PLAYER;
    v->selection = 0;
    v->top = 0;
    return true;
}
void nabo_radio_view_tick(int battery) {
    if (!v)
        return;
    uint32_t now = now_ms();
    nrf_keys_tick(&v->keys, now);
    if (v->undo && !v->keys.pending[NRF_UP])
        v->undo = false;
    if (v->connect_at && static_cast<int32_t>(now - v->connect_at) >= 0) {
        v->connect_at = 0;
        if (nabo_radio_wanted())
            nabo_radio_select_station(v->current);
    }
    int state = static_cast<int>(nabo_radio_state());
    bool playing = state == 3;
    if (state != v->last_state) {
        v->last_state = state;
        if (state == 3) {
            nrf_station_t s;
            nabo_radio_station_info(v->current, &s);
            action(s.types & ((1u << 1) | (1u << 4)) ? 4 : 5);
            rebuild();
        } else if (state == 4)
            action(11);
        else if (state == 6)
            action(8);
        else if (state == 7 || state == 0)
            action(2);
        else if (state == 1 || state == 2 || state == 5) {
            if (v->action != 1)
                action(3);
        }
    }
    unsigned elapsed = now - v->action_at;
    unsigned frame = v->sprite.fps ? elapsed * v->sprite.fps / 1000 : 0;
    if (frame >= v->sprite.frames) {
        if (!v->sprite.loop) {
            action(base_action());
            frame = 0;
        } else
            frame %= v->sprite.frames;
    }
    if (playing && now >= v->sip_at) {
        action(6);
        v->sip_at = now + 60000 + esp_random() % 60000;
        frame = 0;
    }
    if (playing && (v->action == 4 || v->action == 12) && elapsed >= 18000) {
        action(v->action == 4 ? 12 : 4);
        frame = 0;
    }
    unsigned remaining = nabo_radio_sleep_remaining();
    if (playing && remaining && remaining <= 60 && v->action != 9) {
        action(9);
        frame = 0;
    }
    nrf_sprite_frame(&v->sprite, frame);
    v->level = (v->level * 3 + (playing ? nabo_radio_meter() : 0)) / 4;
    if (playing && v->page == PLAYER && now - v->last_key >= 60000)
        v->clock = true;
    if (v->clock != v->dimmed) {
        v->dimmed = v->clock;
        Board::GetInstance().GetBacklight()->SetBrightness(
            v->clock ? std::min(15, v->brightness) : v->brightness, false);
    }
    nrf_gfx_begin(&v->gfx);
    rect(0, 0, 240, 320, BG);
    if (v->clock)
        clock_page();
    else {
        status(battery);
        stage(now);
        info();
        dial(now);
        if (v->page != PLAYER)
            overlay();
        hints(now);
    }
    if (v->gfx.overflow) {
        ESP_LOGE("NABO-FM", "display list exhausted");
        return;
    }
    for (int y = 0; y < 320; y += 8) {
        nrf_render(&v->gfx, y, v->strip);
        uint32_t hash = 2166136261u;
        for (int i = 0; i < 240 * 8; i++)
            hash = (hash ^ v->strip[i]) * 16777619u;
        if (!v->refresh && hash == v->hashes[y / 8])
            continue;
        v->hashes[y / 8] = hash;
        for (int i = 0; i < 240 * 8; i++)
            v->strip[i] = static_cast<uint16_t>((v->strip[i] >> 8) | (v->strip[i] << 8));
        esp_lcd_panel_draw_bitmap(v->panel, 0, y, 240, y + 8, v->strip);
        // SPI parameter transfer waits for the queued color DMA before reusing
        // its only 3.8 KiB source or returning ownership to LVGL.
        esp_lcd_panel_io_tx_param(v->io, 0, nullptr, 0);
    }
    v->refresh = false;
}
