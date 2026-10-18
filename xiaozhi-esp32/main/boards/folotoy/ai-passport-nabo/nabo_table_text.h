#pragma once
// Text for NABO TABLE's direct-to-panel renderer: UTF-8 laid out with the board's 4 bpp
// lv_font_conv fonts, read directly (no LVGL glyph cache). Glyphs of every text on screen
// share one pool, so a frame's text costs only what it uses.
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NT_GLYPH_POOL 240

typedef struct {
    const uint8_t *bitmap;  // 4 bpp, rows packed without padding, high nibble first
    int16_t x, y;           // top-left in screen pixels
    uint8_t w, h;
} nt_glyph_t;

typedef struct {
    nt_glyph_t g[NT_GLYPH_POOL];
    int used;
} nt_glyph_pool_t;

typedef struct {
    uint16_t first, count;  // range in the pool
    int16_t x0, y0, x1, y1; // bounding box, exclusive end
    int16_t w;              // advance width
} nt_text_t;

typedef enum { NT_ALIGN_LEFT, NT_ALIGN_CENTER, NT_ALIGN_RIGHT } nt_align_t;

// Lays out text whose baseline sits at y. Returns false when the pool is full.
bool nt_text_layout(nt_glyph_pool_t *pool, nt_text_t *t, const lv_font_t *font, const char *utf8, int x, int y,
                    nt_align_t align);
int nt_text_width(const lv_font_t *font, const char *utf8);
// Blends row y, columns [x0, x1) of the text into native RGB565 pixels.
void nt_text_row(const nt_glyph_pool_t *pool, const nt_text_t *t, int y, int x0, int x1, uint16_t *row,
                 uint16_t color);

uint16_t nt_blend565(uint16_t dst, uint16_t src, int a);   // a: 0..256

#ifdef __cplusplus
}
#endif
