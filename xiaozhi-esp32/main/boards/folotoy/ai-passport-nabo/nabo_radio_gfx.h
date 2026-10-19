#pragma once
#include "nabo_radio_sprites.h"
#define NRF_W 240
#define NRF_H 320
#define NRF_STRIP 8
#define NRF_COMMANDS 224
typedef struct {
    const uint8_t* data;
    size_t size;
    unsigned count, pixels;
} nrf_font_t;
typedef struct {
    int kind, x, y, w, h;
    uint16_t color;
    const void* data;
    const nrf_font_t* font;
    const char* text;
} nrf_cmd_t;
typedef struct {
    unsigned count, text_used;
    bool overflow;
    nrf_cmd_t cmd[NRF_COMMANDS];
    char text[2048];
} nrf_gfx_t;
bool nrf_font_init(nrf_font_t* f, const void* data, size_t size);
int nrf_text_width(const nrf_font_t* f, const char* text);
void nrf_gfx_begin(nrf_gfx_t* g);
void nrf_rect(nrf_gfx_t* g, int x, int y, int w, int h, uint16_t color);
void nrf_circle(nrf_gfx_t* g, int x, int y, int radius, uint16_t color);
void nrf_line(nrf_gfx_t* g, int x, int y, int x2, int y2, uint16_t color);
/* y is baseline, align -1 left / 0 center / 1 right. Text is clipped to maxwidth. */
void nrf_text(nrf_gfx_t* g, const nrf_font_t* f, const char* text, int x, int y, int maxwidth,
              int align, uint16_t color);
void nrf_sprite(nrf_gfx_t* g, const nrf_sprite_t* s, int x, int y);
void nrf_prop(nrf_gfx_t* g, const void* data, int x, int y);
void nrf_stage(nrf_gfx_t* g);
/* RGB565 in host order. Driver wrapper swaps bytes after dirty-strip hashing. */
void nrf_render(const nrf_gfx_t* g, int y, uint16_t* pixels);
