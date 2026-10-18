#pragma once
// Turns the NABO TABLE model into a display list, screen by screen (a port of the
// prototype's draw functions, laid out for the badge fonts).
#include "nabo_table_gfx.h"
#include "nabo_table_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const lv_font_t *f12, *f16, *f18, *f24, *mono, *poster;
} nt_fonts_t;

void nt_scene_build(ntg_t *g, const nt_t *m, const nt_fonts_t *f);

#ifdef __cplusplus
}
#endif
