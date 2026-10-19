#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NFM1 is read in place from the assets partition. Only pixels lives in RAM. */
typedef struct {
    const uint8_t* data;
    size_t size;
    uint8_t* pixels;
    size_t capacity;
    unsigned count, action, width, height, fps, frames, frame;
    uint32_t palette, offsets;
    bool loop, valid;
} nrf_sprite_t;
bool nrf_sprite_init(nrf_sprite_t* s, const void* data, size_t size, void* pixels, size_t capacity);
bool nrf_sprite_select(nrf_sprite_t* s, const char* name);
/* Random access replays the preceding deltas. Looping to zero clears all pixels. */
bool nrf_sprite_frame(nrf_sprite_t* s, unsigned frame);
uint16_t nrf_sprite_color(const nrf_sprite_t* s, unsigned index);
