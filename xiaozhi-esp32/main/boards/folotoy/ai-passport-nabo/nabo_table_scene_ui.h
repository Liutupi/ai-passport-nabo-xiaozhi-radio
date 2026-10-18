#pragma once
// Shared pieces of the NABO TABLE screens (nabo_table_scene.c), used by the game tables
// (nabo_table_scene_play.c).
#include "nabo_table_scene.h"

#define NT_COL_FELT_TEXT ntg_rgb(244, 241, 232)
#define NT_COL_MUTED ntg_rgb(143, 179, 168)
#define NT_COL_BRASS ntg_rgb(201, 162, 74)
#define NT_COL_INK ntg_rgb(20, 16, 14)
#define NT_COL_RED ntg_rgb(210, 58, 58)
#define NT_COL_P1 ntg_rgb(255, 138, 61)
#define NT_COL_P2 ntg_rgb(61, 184, 255)
#define NT_COL_P3 ntg_rgb(255, 111, 168)
#define NT_COL_P4 ntg_rgb(142, 219, 90)
#define NT_COL_P5 ntg_rgb(255, 214, 92)
#define NT_COL_P6 ntg_rgb(178, 140, 255)
#define NT_COL_P7 ntg_rgb(72, 220, 200)
#define NT_COL_P8 ntg_rgb(255, 128, 128)
#define NT_COL_WALL ntg_rgb(10, 26, 23)
#define NT_PLAY_RIM 92

extern const char *const NT_PNAME[NT_UC_MAX];
uint16_t nt_pcol(int p);
// Three or four players sit in a 2 x 2 grid of cells NT_CELL_W wide: left column x 8, right
// column x 124, the third of three alone in the middle of the second row.
#define NT_CELL_W 108
int nt_cell_x(const nt_t *m, int p);

typedef struct { const char *text; uint16_t color; } nt_seg_t;
// One line of differently coloured pieces joined by " · ", vertical middle at y.
void nt_seg_line(ntg_t *g, const nt_fonts_t *f, int x, int y, const nt_seg_t *segs, int n, nt_align_t align);
// The same pieces run together without separators (a sentence with a coloured name in it).
void nt_runs(ntg_t *g, const lv_font_t *font, int x, int y, const nt_seg_t *segs, int n, nt_align_t align);

// Text whose vertical middle sits at y (the prototype's textBaseline = 'middle').
void nt_txt(ntg_t *g, const lv_font_t *font, const char *s, int x, int y, nt_align_t align, uint16_t color);
int nt_ease_out(int t256);   // 1 - (1 - t)^3, 0..256
// The dealer at the table edge: sprite id for the model's animation, at the current frame.
void nt_dealer(ntg_t *g, const nt_t *m, int rim, bool large);
void nt_scores(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
void nt_chip(ntg_t *g, const nt_fonts_t *f, int p, const char *label);
void nt_hint(ntg_t *g, const nt_fonts_t *f, const char *s);
void nt_choices(ntg_t *g, const nt_fonts_t *f, int y, const char *a, const char *b, int cursor, uint16_t color);
void nt_result_banner(ntg_t *g, const nt_t *m, const nt_fonts_t *f, const char *main, const char *why, uint16_t col);
// "4 个 [die]" with a big number and a die of die_px, centred (align CENTER) or starting (LEFT)
// at x, vertical middle at y. Sized to be read across the table. Returns the width used.
int nt_bid_label(ntg_t *g, const nt_fonts_t *f, int x, int y, int q, int face, nt_align_t align, uint16_t color,
                 int die_px);

void nt_scene_bj(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
void nt_scene_bj_turn(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
void nt_scene_dice(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
void nt_scene_dice_turn(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
void nt_scene_uc(ntg_t *g, const nt_t *m, const nt_fonts_t *f);          // setup, word, talk, vote, out, end
void nt_scene_uc_handoff(ntg_t *g, const nt_t *m, const nt_fonts_t *f);
