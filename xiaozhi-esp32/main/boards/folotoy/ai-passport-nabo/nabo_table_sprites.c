#include "nabo_table_sprites.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool nts_open(nts_pack_t *p, const void *data, size_t size)
{
    const uint8_t *d = (const uint8_t *)data;
    memset(p, 0, sizeof(*p));
    if (!d || size < 16 || memcmp(d, "NTS1", 4) != 0) return false;
    p->data = d;
    p->size = size;
    p->anim_count = rd16(d + 4);
    p->frame_count = rd16(d + 6);
    p->anims_off = rd32(d + 8);
    p->frames_off = rd32(d + 12);
    return p->anims_off + 32u * p->anim_count <= size && p->frames_off + 8u * p->frame_count <= size;
}

bool nts_anim(const nts_pack_t *p, int anim, nts_anim_t *a)
{
    if (anim < 0 || anim >= p->anim_count) return false;
    const uint8_t *e = p->data + p->anims_off + 32u * anim;
    a->frames = e[13];
    a->fps = e[14];
    a->loop = e[15] & 1;
    a->keyframes = (e[15] & 2) != 0;
    a->w = rd16(e + 16);
    a->h = rd16(e + 18);
    a->ox = (int16_t)rd16(e + 20);
    a->oy = (int16_t)rd16(e + 22);
    a->first = rd16(e + 24);
    a->colors = rd16(e + 26);
    a->palette = rd32(e + 28);
    return a->colors <= 255 && a->first + a->frames <= p->frame_count;
}

size_t nts_buffer_need(const nts_pack_t *p)
{
    size_t need = 0;
    nts_anim_t a;
    for (int i = 0; i < p->anim_count; i++)
        if (nts_anim(p, i, &a) && !a.keyframes && (size_t)a.w * a.h > need) need = (size_t)a.w * a.h;
    return need;
}

// Row data of frame f, row y: [start, end) bytes.
static const uint8_t *row_data(const nts_player_t *pl, int frame, int y, const uint8_t **end)
{
    const nts_pack_t *p = pl->pack;
    const uint8_t *fe = p->data + p->frames_off + 8u * (pl->a.first + frame);
    const uint8_t *rows = p->data + rd32(fe), *rle = p->data + rd32(fe + 4);
    *end = rle + rd16(rows + 2 * (y + 1));
    return rle + rd16(rows + 2 * y);
}

static void decode_key_row(const uint8_t *s, const uint8_t *e, uint8_t *out)
{
    int x = 0;
    while (s < e) {
        unsigned t = *s++;
        int run = (int)(t & 127) + ((t & 128) ? 2 : 1);
        if (t & 128) {
            memset(out + x, *s++, (size_t)run);
        } else {
            memcpy(out + x, s, (size_t)run);
            s += run;
        }
        x += run;
    }
}

static void apply_delta_row(const uint8_t *s, const uint8_t *e, uint8_t *row)
{
    int x = 0;
    while (s < e) {
        unsigned t = *s++;
        int run = (int)(t & 127) + 1;
        if (!(t & 128)) {
            memcpy(row + x, s, (size_t)run);
            s += run;
        }
        x += run;
    }
}

void nts_player_init(nts_player_t *pl, const nts_pack_t *p, uint8_t *buf, size_t buf_size)
{
    memset(pl, 0, sizeof(*pl));
    pl->pack = p;
    pl->buf = buf;
    pl->buf_size = buf_size;
    pl->anim = -1;
    pl->frame = -1;
}

static uint16_t rgb565(const uint8_t *c) { return (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3); }

bool nts_player_set(nts_player_t *pl, int anim, int frame)
{
    if (anim != pl->anim) {
        if (!nts_anim(pl->pack, anim, &pl->a)) return false;
        if (!pl->a.keyframes && (size_t)pl->a.w * pl->a.h > pl->buf_size) return false;
        const uint8_t *pal = pl->pack->data + pl->a.palette;
        for (int i = 0; i < pl->a.colors; i++) pl->pal[i + 1] = rgb565(pal + 3 * i);
        pl->anim = anim;
        pl->frame = -1;
    }
    if (frame < 0) frame = 0;
    if (frame >= pl->a.frames) frame = pl->a.frames - 1;
    if (pl->a.keyframes) {
        pl->frame = frame;
        return true;
    }
    const uint8_t *s, *e;
    if (pl->frame < 0 || frame < pl->frame) {
        for (int y = 0; y < pl->a.h; y++) {
            s = row_data(pl, 0, y, &e);
            decode_key_row(s, e, pl->buf + (size_t)y * pl->a.w);
        }
        pl->frame = 0;
    }
    while (pl->frame < frame) {
        pl->frame++;
        for (int y = 0; y < pl->a.h; y++) {
            s = row_data(pl, pl->frame, y, &e);
            apply_delta_row(s, e, pl->buf + (size_t)y * pl->a.w);
        }
    }
    return true;
}

void nts_player_row(const nts_player_t *pl, int sy, int sx, int x0, int x1, uint16_t *dst)
{
    if (pl->frame < 0 || sy < 0 || sy >= pl->a.h || sx >= x1 || sx + pl->a.w <= x0) return;
    if (!pl->a.keyframes) {
        const uint8_t *row = pl->buf + (size_t)sy * pl->a.w;
        int a = sx > x0 ? sx : x0, b = sx + pl->a.w < x1 ? sx + pl->a.w : x1;
        for (int x = a; x < b; x++)
            if (row[x - sx]) dst[x - x0] = pl->pal[row[x - sx]];
        return;
    }
    // Keyframe straight from flash: walk the runs, write only what falls inside [x0, x1).
    const uint8_t *e, *s = row_data(pl, pl->frame, sy, &e);
    int x = sx;
    while (s < e && x < x1) {
        unsigned t = *s++;
        int run = (int)(t & 127) + ((t & 128) ? 2 : 1);
        int a = x > x0 ? x : x0, b = x + run < x1 ? x + run : x1;
        if (t & 128) {
            unsigned v = *s++;
            if (v)
                for (int k = a; k < b; k++) dst[k - x0] = pl->pal[v];
        } else {
            for (int k = a; k < b; k++)
                if (s[k - x]) dst[k - x0] = pl->pal[s[k - x]];
            s += run;
        }
        x += run;
    }
}
