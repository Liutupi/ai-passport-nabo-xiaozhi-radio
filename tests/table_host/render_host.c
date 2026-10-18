// Host renderer for test_table.py: draws the model with the real NABO TABLE renderer into a
// 240x320 framebuffer, exactly what the panel would receive. A second renderer always redraws
// the whole screen, so the dirty-rectangle path can be checked pixel for pixel.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nabo_table_scene.h"

LV_FONT_DECLARE(nabo_table_font_12)
LV_FONT_DECLARE(nabo_menu_font_16)
LV_FONT_DECLARE(nabo_ui_font_18)
LV_FONT_DECLARE(nabo_table_font_24)
LV_FONT_DECLARE(nabo_mono_font_12)
LV_FONT_DECLARE(nabo_poster_font_28)

nt_t *host_model(void);

typedef struct {
    ntg_t g;
    uint16_t fb[NTG_W * NTG_H];
    uint16_t strip[2][NTG_W * NTG_STRIP];
    uint8_t *sprite_buf;
} target_t;

static nts_pack_t pack;
static uint8_t *pack_data;
static target_t *inc, *full;
static nt_fonts_t fonts;

static void send(void *ctx, int x, int y, int w, int h, uint16_t *px)
{
    target_t *t = ctx;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            uint16_t v = px[r * w + c];
            t->fb[(y + r) * NTG_W + x + c] = (uint16_t)(v << 8 | v >> 8);   // back to native
        }
}

static target_t *make_target(void)
{
    target_t *t = calloc(1, sizeof(*t));
    size_t need = nts_buffer_need(&pack);
    t->sprite_buf = malloc(need);
    ntg_init(&t->g, &pack, t->sprite_buf, need, t->strip[0], t->strip[1], send, t);
    return t;
}

int host_render_init(const char *pack_path)
{
    FILE *f = fopen(pack_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    pack_data = malloc((size_t)n);
    if (fread(pack_data, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -2; }
    fclose(f);
    if (!nts_open(&pack, pack_data, (size_t)n)) return -3;
    fonts.f12 = &nabo_table_font_12;
    fonts.f16 = &nabo_menu_font_16;
    fonts.f18 = &nabo_ui_font_18;
    fonts.f24 = &nabo_table_font_24;
    fonts.mono = &nabo_mono_font_12;
    fonts.poster = &nabo_poster_font_28;
    inc = make_target();
    full = make_target();
    return (int)nts_buffer_need(&pack);
}

// Draws one frame incrementally; returns bytes sent. With check set, also redraws everything
// on the second target and returns -1 when the two framebuffers differ.
int host_draw(int check)
{
    nt_scene_build(&inc->g, host_model(), &fonts);
    ntg_flush(&inc->g);
    int bytes = (int)inc->g.bytes;
    if (!check) return bytes;
    ntg_invalidate(&full->g);
    nt_scene_build(&full->g, host_model(), &fonts);
    ntg_flush(&full->g);
    return memcmp(inc->fb, full->fb, sizeof(inc->fb)) ? -1 : bytes;
}

void host_fb(uint16_t *out) { memcpy(out, inc->fb, sizeof(inc->fb)); }
int host_ops(void) { return inc->g.n; }
int host_glyphs(void) { return inc->g.glyphs.used; }
