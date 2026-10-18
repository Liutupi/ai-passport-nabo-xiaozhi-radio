#pragma once
// NABO TABLE sprite pack reader (format: tools/generate_table_sprites.py). The pack lives in
// the assets partition and is read in place; large animations decode each row straight from
// flash, small ones keep one index buffer per player so delta frames can be applied.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nabo_table_sprite_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t frames, fps;
    bool loop, keyframes;   // keyframes: every frame stands alone (no buffer needed)
    uint16_t w, h;
    int16_t ox, oy;         // top-left relative to the table-line anchor
    uint16_t first, colors;
    uint32_t palette;       // byte offset of colors x RGB888
} nts_anim_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t anim_count, frame_count;
    uint32_t anims_off, frames_off;
} nts_pack_t;

// One playing animation. buf is caller-owned, at least w x h bytes for delta animations.
typedef struct {
    const nts_pack_t *pack;
    nts_anim_t a;
    int anim, frame;        // frame currently held in buf (-1: none)
    uint8_t *buf;
    size_t buf_size;
    uint16_t pal[256];      // native RGB565, index 0 unused (transparent)
} nts_player_t;

bool nts_open(nts_pack_t *p, const void *data, size_t size);
bool nts_anim(const nts_pack_t *p, int anim, nts_anim_t *out);
// Largest w x h among the delta (buffered) animations: the buffer the game must provide.
size_t nts_buffer_need(const nts_pack_t *p);

void nts_player_init(nts_player_t *pl, const nts_pack_t *p, uint8_t *buf, size_t buf_size);
// Selects an animation and frame; decodes forward from the keyframe when needed.
bool nts_player_set(nts_player_t *pl, int anim, int frame);
// Blends sprite row sy (0..h-1) into dst, which covers screen columns [x0, x1); the sprite's
// left edge is at screen column sx. Transparent pixels leave dst untouched.
void nts_player_row(const nts_player_t *pl, int sy, int sx, int x0, int x1, uint16_t *dst);

#ifdef __cplusplus
}
#endif
