#include "nabo_radio_gfx.h"
#include <stdlib.h>
#include <string.h>
static unsigned u16(const uint8_t* p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t u32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int s16(const uint8_t* p) { return (int16_t)u16(p); }
bool nrf_font_init(nrf_font_t* f, const void* data, size_t size) {
    memset(f, 0, sizeof(*f));
    if (!data || size < 8 || memcmp(data, "NFF1", 4))
        return false;
    const uint8_t* p = data;
    unsigned count = u16(p + 4);
    if (!count || 8 + 16 * count > size)
        return false;
    uint32_t last = 0;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t* d = p + 8 + 16 * i;
        uint32_t at = u32(d + 4), cp = u32(d);
        if (cp <= last || at < 8 + 16 * count || at > size ||
            ((size_t)d[14] * d[15] + 1) / 2 > size - at)
            return false;
        last = cp;
    }
    f->data = data;
    f->size = size;
    f->count = count;
    f->pixels = u16(p + 6);
    return true;
}
static uint32_t next(const char** str) {
    const unsigned char* p = (const unsigned char*)*str;
    unsigned c = *p++;
    if (c >= 0xc0 && c < 0xe0 && p[0]) {
        c = (c & 31) << 6 | (p[0] & 63);
        p++;
    } else if (c >= 0xe0 && c < 0xf0 && p[0] && p[1]) {
        c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63);
        p += 2;
    } else if (c >= 0xf0) {
        while ((*p & 0xc0) == 0x80)
            p++;
        c = '?';
    }
    *str = (const char*)p;
    return c;
}
static const uint8_t* glyph(const nrf_font_t* f, uint32_t cp) {
    if (!f || !f->data)
        return NULL;
    unsigned lo = 0, hi = f->count;
    while (lo < hi) {
        unsigned m = (lo + hi) / 2;
        const uint8_t* p = f->data + 8 + 16 * m;
        if (u32(p) < cp)
            lo = m + 1;
        else
            hi = m;
    }
    if (lo < f->count && u32(f->data + 8 + 16 * lo) == cp)
        return f->data + 8 + 16 * lo;
    return cp == '?' ? NULL : glyph(f, '?');
}
int nrf_text_width(const nrf_font_t* f, const char* s) {
    int w = 0;
    if (!s)
        return 0;
    while (*s) {
        const uint8_t* p = glyph(f, next(&s));
        if (p)
            w += s16(p + 8);
    }
    return w;
}
static nrf_cmd_t* add(nrf_gfx_t* g, int kind, int x, int y, int w, int h, uint16_t color) {
    if (g->count >= NRF_COMMANDS) {
        g->overflow = true;
        return NULL;
    }
    nrf_cmd_t* c = &g->cmd[g->count++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->color = color;
    return c;
}
void nrf_gfx_begin(nrf_gfx_t* g) {
    g->count = 0;
    g->text_used = 0;
    g->overflow = false;
}
void nrf_rect(nrf_gfx_t* g, int x, int y, int w, int h, uint16_t c) { add(g, 0, x, y, w, h, c); }
void nrf_circle(nrf_gfx_t* g, int x, int y, int r, uint16_t c) {
    add(g, 1, x - r, y - r, 2 * r + 1, 2 * r + 1, c);
}
void nrf_line(nrf_gfx_t* g, int x, int y, int x2, int y2, uint16_t c) {
    add(g, 2, x, y, x2, y2, c);
}
void nrf_text(nrf_gfx_t* g, const nrf_font_t* f, const char* s, int x, int y, int maxw, int align,
              uint16_t color) {
    if (!f || !f->data || !s)
        return;
    nrf_cmd_t* c = add(g, 3, x, y, maxw, 0, color);
    if (!c)
        return;
    c->font = f;
    int w = 0;
    size_t bytes = 0;
    char* dst = g->text + g->text_used;
    while (*s) {
        const char* start = s;
        const uint8_t* p = glyph(f, next(&s));
        int a = p ? s16(p + 8) : 0;
        size_t n = (size_t)(s - start);
        if (w + a > maxw || bytes + n >= 127)
            break;
        if (g->text_used + bytes + n + 1 > sizeof(g->text)) {
            g->overflow = true;
            break;
        }
        memcpy(dst + bytes, start, n);
        bytes += n;
        w += a;
    }
    if (g->text_used + bytes >= sizeof(g->text)) {
        g->count--;
        return;
    }
    dst[bytes] = 0;
    c->text = dst;
    g->text_used += (unsigned)bytes + 1;
    c->x -= align == 0 ? w / 2 : align > 0 ? w : 0;
}
void nrf_sprite(nrf_gfx_t* g, const nrf_sprite_t* s, int x, int y) {
    if (!s || !s->valid)
        return;
    nrf_cmd_t* c = add(g, 4, x, y, (int)s->width, (int)s->height, 0);
    if (c)
        c->data = s;
}
void nrf_prop(nrf_gfx_t* g, const void* data, int x, int y) {
    if (!data || memcmp(data, "NFP1", 4))
        return;
    const uint8_t* p = data;
    nrf_cmd_t* c = add(g, 5, x, y, (int)u16(p + 4), (int)u16(p + 6), 0);
    if (c)
        c->data = p + 8;
}
void nrf_stage(nrf_gfx_t* g) { add(g, 6, 0, 18, 240, 132, 0x10a4); }
static uint16_t blend(uint16_t a, uint16_t b, unsigned alpha) {
    unsigned r = (((a >> 11) * alpha + (b >> 11) * (255 - alpha) + 127) / 255);
    unsigned gg = ((((a >> 5) & 63) * alpha + ((b >> 5) & 63) * (255 - alpha) + 127) / 255);
    unsigned bb = (((a & 31) * alpha + (b & 31) * (255 - alpha) + 127) / 255);
    return (uint16_t)((r << 11) | (gg << 5) | bb);
}
static void put(uint16_t* p, int y0, int x, int y, uint16_t c, unsigned a) {
    if (x < 0 || x >= NRF_W || y < y0 || y >= y0 + NRF_STRIP)
        return;
    unsigned at = (unsigned)(y - y0) * NRF_W + (unsigned)x;
    p[at] = a == 255 ? c : blend(c, p[at], a);
}
void nrf_render(const nrf_gfx_t* g, int y0, uint16_t* px) {
    memset(px, 0, NRF_W * NRF_STRIP * 2);
    for (unsigned k = 0; k < g->count; k++) {
        const nrf_cmd_t* c = &g->cmd[k];
        if (c->kind == 2) {
            int x = c->x, y = c->y, dx = abs(c->w - x), sx = x < c->w ? 1 : -1, dy = -abs(c->h - y),
                sy = y < c->h ? 1 : -1, e = dx + dy;
            for (;;) {
                put(px, y0, x, y, c->color, 255);
                if (x == c->w && y == c->h)
                    break;
                int ee = 2 * e;
                if (ee >= dy) {
                    e += dy;
                    x += sx;
                }
                if (ee <= dx) {
                    e += dx;
                    y += sy;
                }
            }
            continue;
        }
        if (c->kind == 3) {
            if (c->y < y0 || c->y - (int)c->font->pixels > y0 + NRF_STRIP)
                continue;
            int x = c->x;
            const char* s = c->text;
            while (*s) {
                const uint8_t* d = glyph(c->font, next(&s));
                if (!d)
                    continue;
                int w = d[14], h = d[15], left = x + s16(d + 10), top = c->y + s16(d + 12);
                const uint8_t* bitmap = c->font->data + u32(d + 4);
                for (int yy = 0; yy < h; yy++) {
                    int y = top + yy;
                    if (y < y0 || y >= y0 + NRF_STRIP)
                        continue;
                    for (int xx = 0; xx < w; xx++) {
                        unsigned at = (unsigned)(yy * w + xx);
                        unsigned a = (bitmap[at / 2] >> (at % 2 ? 0 : 4)) & 15;
                        if (a)
                            put(px, y0, left + xx, y, c->color, a * 17);
                    }
                }
                x += s16(d + 8);
            }
            continue;
        }
        int ya = c->y > y0 ? c->y : y0,
            yb = c->y + c->h < y0 + NRF_STRIP ? c->y + c->h : y0 + NRF_STRIP;
        int xa = c->x > 0 ? c->x : 0, xb = c->x + c->w < NRF_W ? c->x + c->w : NRF_W;
        for (int y = ya; y < yb; y++)
            for (int x = xa; x < xb; x++) {
                uint16_t color = c->color;
                unsigned a = 255;
                if (c->kind == 6) {
                    if ((x - 8) % 24 == 0)
                        color = 0x10e5;
                    if (y >= 26 && (y - 26) % 10 < 2 && (x - 4 - ((y / 10) % 2) * 5 + 240) % 10 < 2)
                        color = 0x1906;
                }
                if (c->kind == 1) {
                    int r = c->w / 2, dx = x - c->x - r, dy = y - c->y - r;
                    if (dx * dx + dy * dy > r * r)
                        continue;
                }
                if (c->kind == 4) {
                    const nrf_sprite_t* s = c->data;
                    unsigned index = s->pixels[(y - c->y) * c->w + x - c->x];
                    if (!index)
                        continue;
                    color = nrf_sprite_color(s, index);
                }
                if (c->kind == 5) {
                    const uint8_t* p = (const uint8_t*)c->data + 3 * ((y - c->y) * c->w + x - c->x);
                    color = (uint16_t)u16(p);
                    a = p[2];
                }
                put(px, y0, x, y, color, a);
            }
    }
}
