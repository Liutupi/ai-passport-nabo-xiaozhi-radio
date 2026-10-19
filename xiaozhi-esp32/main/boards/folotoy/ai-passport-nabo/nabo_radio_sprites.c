#include "nabo_radio_sprites.h"
#include <string.h>

static unsigned u16(const uint8_t* p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t u32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool range(const nrf_sprite_t* s, size_t at, size_t n) {
    return at <= s->size && n <= s->size - at;
}
bool nrf_sprite_init(nrf_sprite_t* s, const void* data, size_t size, void* pixels,
                     size_t capacity) {
    if (!s)
        return false;
    memset(s, 0, sizeof(*s));
    if (!data || !pixels || size < 8 || memcmp(data, "NFM1", 4))
        return false;
    s->data = data;
    s->size = size;
    s->pixels = pixels;
    s->capacity = capacity;
    s->count = u16(s->data + 4);
    if (!s->count || s->count > 32 || !range(s, 8, 32 * s->count))
        return false;
    s->action = s->count;
    return true;
}
bool nrf_sprite_select(nrf_sprite_t* s, const char* name) {
    if (!s || !s->data || !name)
        return false;
    for (unsigned i = 0; i < s->count; i++) {
        const uint8_t* p = s->data + 8 + 32 * i;
        if (strncmp((const char*)p, name, 12))
            continue;
        unsigned w = u16(p + 12), h = u16(p + 14), nf = u16(p + 18);
        uint32_t pal = u32(p + 20), off = u32(p + 24);
        if (!w || !h || w > 240 || h > 320 || (size_t)w * h > s->capacity || !p[16] || p[16] > 60 ||
            !nf || nf > 1024 || !range(s, pal, 128) || !range(s, off, nf * 4))
            return false;
        s->action = i;
        s->width = w;
        s->height = h;
        s->fps = p[16];
        s->loop = p[17] != 0;
        s->frames = nf;
        s->palette = pal;
        s->offsets = off;
        s->valid = false;
        return nrf_sprite_frame(s, 0);
    }
    return false;
}
static bool decode(nrf_sprite_t* s, unsigned f) {
    size_t at = u32(s->data + s->offsets + 4 * f);
    size_t end = f + 1 < s->frames ? u32(s->data + s->offsets + 4 * (f + 1)) : s->size;
    if (end < at || end > s->size || !range(s, at, 2 * s->height) || at + 2 * s->height > end)
        return false;
    if (!f)
        memset(s->pixels, 0, s->width * s->height);
    for (unsigned y = 0; y < s->height; y++) {
        unsigned ro = u16(s->data + at + 2 * y);
        size_t p = at + ro;
        size_t row_end = y + 1 < s->height ? at + u16(s->data + at + 2 * (y + 1)) : end;
        if (ro < 2 * s->height || p >= row_end || row_end > end)
            return false;
        unsigned x = 0;
        bool ended = false;
        while (p < row_end) {
            unsigned c = s->data[p++], n = (c & 63) + 1, kind = c >> 6;
            if (kind == 3) {
                if (c != 0xc0)
                    return false;
                ended = true;
                break;
            }
            if (n > s->width - x)
                return false;
            uint8_t* dst = s->pixels + y * s->width + x;
            if (kind == 1) {
                if (n > row_end - p)
                    return false;
                for (unsigned j = 0; j < n; j++)
                    if (s->data[p + j] >= 64)
                        return false;
                memcpy(dst, s->data + p, n);
                p += n;
            } else if (kind == 2) {
                if (p >= row_end || s->data[p] >= 64)
                    return false;
                memset(dst, s->data[p++], n);
            }
            x += n;
        }
        if (!ended)
            return false;
    }
    return true;
}
bool nrf_sprite_frame(nrf_sprite_t* s, unsigned frame) {
    if (!s || s->action >= s->count || frame >= s->frames)
        return false;
    unsigned first = s->valid && frame >= s->frame ? s->frame + 1 : 0;
    for (unsigned i = first; i <= frame; i++) {
        if (!decode(s, i)) {
            s->valid = false;
            return false;
        }
    }
    s->frame = frame;
    s->valid = true;
    return true;
}
uint16_t nrf_sprite_color(const nrf_sprite_t* s, unsigned index) {
    return s && index < 64 && range(s, s->palette + 2 * index, 2)
               ? (uint16_t)u16(s->data + s->palette + 2 * index)
               : 0;
}
