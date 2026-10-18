// NABO TABLE screens shared by both games: cover menu, hand-off, match end, pause, plus the
// common pieces (dealer sprite, scores, chips, hints, result banner, bid label).
#include "nabo_table_scene_ui.h"

#include <stdio.h>

const char *const NT_PNAME[2] = {"玩家 1", "玩家 2"};
uint16_t nt_pcol(int p) { return p ? NT_COL_P2 : NT_COL_P1; }

void nt_txt(ntg_t *g, const lv_font_t *font, const char *s, int x, int y, nt_align_t align, uint16_t color)
{
    int baseline = y + (font->line_height - 2 * font->base_line) / 2;
    ntg_text(g, font, s, x, baseline, align, color);
}

int nt_ease_out(int t)
{
    t = t < 0 ? 0 : t > 256 ? 256 : t;
    int u = 256 - t;
    return 256 - (u * u / 256) * u / 256;
}

// ---------- dealer ----------

static int sprite_for(const nt_t *m, bool large)
{
    if (large) {
        switch (m->anim) {
        case NT_ANIM_SHUFFLE: return NTS_LARGE_SPREAD;
        case NT_ANIM_SHAKE: return NTS_LARGE_SHAKE;
        case NT_ANIM_COVER_IN: return NTS_LARGE_COVER_IN;
        case NT_ANIM_COVER: return NTS_LARGE_COVER;
        case NT_ANIM_TROPHY: return NTS_LARGE_TROPHY;
        default: return NTS_LARGE_IDLE_EMPTY;
        }
    }
    switch (m->anim) {
    case NT_ANIM_SHUFFLE: return NTS_SMALL_CUT;
    case NT_ANIM_DEAL: return NTS_SMALL_DEAL;
    case NT_ANIM_SHAKE: return NTS_SMALL_SHAKE;
    case NT_ANIM_SLAM: return NTS_SMALL_SLAM;
    case NT_ANIM_OPEN: return NTS_SMALL_OPEN;
    case NT_ANIM_POINT: return m->mirror ? NTS_SMALL_POINT_R : NTS_SMALL_POINT_L;
    case NT_ANIM_SURPRISE: return NTS_SMALL_SURPRISE;
    default: return m->game == NT_GAME_BJ ? NTS_SMALL_IDLE_CARDS : NTS_SMALL_IDLE_EMPTY;
    }
}

void nt_dealer(ntg_t *g, const nt_t *m, int rim, bool large)
{
    int id = sprite_for(m, large);
    nts_anim_t a;
    if (!nts_anim(g->sprite.pack, id, &a)) return;
    int f = (int)(m->anim_t * a.fps);
    f = a.loop ? f % a.frames : (f < a.frames ? f : a.frames - 1);
    ntg_sprite(g, id, f, NTG_W / 2, rim);
}

// ---------- common pieces ----------

void nt_scores(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    char s[8];
    snprintf(s, sizeof(s), "%d", m->score[0]);
    nt_txt(g, f->poster, s, 8, 18, NT_ALIGN_LEFT, NT_COL_P1);
    snprintf(s, sizeof(s), "%d", m->score[1]);
    nt_txt(g, f->poster, s, NTG_W - 8, 18, NT_ALIGN_RIGHT, NT_COL_P2);
    char t[16];
    snprintf(t, sizeof(t), "抢 %d", m->cfg.target);
    nt_txt(g, f->f12, t, NTG_W / 2, 9, NT_ALIGN_CENTER, NT_COL_MUTED);
}

void nt_chip(ntg_t *g, const nt_fonts_t *f, int p, const char *label)
{
    int w = ntg_text_width(f->f12, label) + 16;
    ntg_rect(g, 8, 32, w, 18, 9, nt_pcol(p), 255, 0, 0);
    nt_txt(g, f->f12, label, 16, 41, NT_ALIGN_LEFT, NT_COL_INK);
}

void nt_hint(ntg_t *g, const nt_fonts_t *f, const char *s) { nt_txt(g, f->f12, s, NTG_W / 2, 310, NT_ALIGN_CENTER, NT_COL_MUTED); }

void nt_choices(ntg_t *g, const nt_fonts_t *f, int y, const char *a, const char *b, int cursor, uint16_t color)
{
    const int w = 92, gap = 12, x0 = (NTG_W - (w * 2 + gap)) / 2;
    for (int i = 0; i < 2; i++) {
        int x = x0 + i * (w + gap);
        bool sel = cursor == i;
        ntg_rect(g, x, y - 10, w, 20, 10, sel ? color : ntg_rgb(255, 255, 255), sel ? 255 : 16, 0, 0);
        nt_txt(g, f->f12, i ? b : a, x + w / 2, y, NT_ALIGN_CENTER, sel ? NT_COL_INK : NT_COL_FELT_TEXT);
    }
}

void nt_result_banner(ntg_t *g, const nt_t *m, const nt_fonts_t *f, const char *why)
{
    int w = m->result.winner;
    uint16_t col = w < 0 ? NT_COL_BRASS : nt_pcol(w);
    char line[96];
    snprintf(line, sizeof(line), "%s · %s", w < 0 ? "平局" : w ? "玩家 2 赢" : "玩家 1 赢", why);
    ntg_rect(g, 0, 266, NTG_W, 54, 0, ntg_rgb(4, 12, 10), 235, 0, 0);
    ntg_rect(g, 0, 266, NTG_W, 2, 0, col, 255, 0, 0);
    nt_txt(g, f->f12, line, NTG_W / 2, 282, NT_ALIGN_CENTER, col);
    nt_choices(g, f, 304, nt_match_over(m) ? "看总比分" : "下一局", "回封面", m->choice, col);
}

int nt_bid_label(ntg_t *g, const nt_fonts_t *f, int x, int y, int q, int face, nt_align_t align, uint16_t color,
                 int die_px)
{
    char s[8];
    snprintf(s, sizeof(s), "%d", q);
    int nw = ntg_text_width(f->poster, s), gw = ntg_text_width(f->f18, "个");
    int total = nw + 4 + gw + 6 + die_px;
    int sx = align == NT_ALIGN_CENTER ? x - total / 2 : x;
    nt_txt(g, f->poster, s, sx, y + 1, NT_ALIGN_LEFT, color);
    nt_txt(g, f->f18, "个", sx + nw + 4, y, NT_ALIGN_LEFT, color);
    // The die rests on the felt: its contact point sits a little below the label's middle.
    ntg_die(g, face, sx + nw + 4 + gw + 6 + die_px / 2, y + die_px * 3 / 10, die_px * 10 / 27, 22, false);
    return total;
}

// ---------- cover, hand-off, end, pause ----------

static void scene_menu(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    static const char *const ITEMS[2] = {"21 点", "大话骰"};
    ntg_bg(g, 168);
    nt_dealer(g, m, 168, true);
    nt_txt(g, f->poster, "NABO", 86, 194, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
    nt_txt(g, f->f18, "牌桌", 154, 195, NT_ALIGN_CENTER, NT_COL_BRASS);
    for (int i = 0; i < NT_MENU_COUNT; i++) {
        int y = 224 + i * 26;
        bool sel = m->menu == i;
        char target[16];
        snprintf(target, sizeof(target), "抢 %d 局", m->cfg.target);
        if (sel) ntg_rect(g, 56, y - 11, 128, 22, 11, NT_COL_BRASS, 255, 0, 0);
        nt_txt(g, f->f16, i < 2 ? ITEMS[i] : target, NTG_W / 2, y, NT_ALIGN_CENTER, sel ? NT_COL_INK : NT_COL_FELT_TEXT);
    }
    nt_hint(g, f, "↑↓ 选择 · OK 进入 · 双击上 退出");
}

static void scene_handoff(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int p = m->handoff_to;
    char s[64];
    ntg_bg(g, 170);
    nt_dealer(g, m, 170, true);
    nt_scores(g, m, f);
    nt_txt(g, f->f12, "请交给", NTG_W / 2, 194, NT_ALIGN_CENTER, NT_COL_MUTED);
    nt_txt(g, f->f24, NT_PNAME[p], NTG_W / 2, 220, NT_ALIGN_CENTER, nt_pcol(p));
    if (m->game == NT_GAME_BJ) {
        snprintf(s, sizeof(s), "玩家 1 %d 张%s · 玩家 2 %d 张%s", m->bj.hand_n[0], m->bj.stood[0] ? " 已停" : "",
                 m->bj.hand_n[1], m->bj.stood[1] ? " 已停" : "");
        nt_txt(g, f->f12, s, NTG_W / 2, 250, NT_ALIGN_CENTER, NT_COL_MUTED);
    } else {
        const nt_bid_t *last = nt_last_bid(m);
        if (last) {
            snprintf(s, sizeof(s), "%s 叫", NT_PNAME[last->p]);
            nt_txt(g, f->f16, s, NTG_W / 2 - 8, 253, NT_ALIGN_RIGHT, nt_pcol(last->p));
            nt_bid_label(g, f, NTG_W / 2, 253, last->q, last->f, NT_ALIGN_LEFT, NT_COL_FELT_TEXT, 24);
        } else {
            nt_txt(g, f->f12, "第一个叫数", NTG_W / 2, 252, NT_ALIGN_CENTER, NT_COL_MUTED);
        }
    }
    ntg_rect(g, 48, 274, 144, 26, 13, nt_pcol(p), 255, 0, 0);
    nt_txt(g, f->f16, "准备好按 OK", NTG_W / 2, 287, NT_ALIGN_CENTER, NT_COL_INK);
    nt_txt(g, f->f12, "长按 OK 暂停", NTG_W / 2, 311, NT_ALIGN_CENTER, NT_COL_MUTED);
}

static void scene_end(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int w = m->score[0] >= m->cfg.target ? 0 : 1;
    char s[16];
    ntg_bg(g, 186);
    nt_dealer(g, m, 186, true);
    snprintf(s, sizeof(s), "%s 获胜", NT_PNAME[w]);
    nt_txt(g, f->f24, s, NTG_W / 2, 216, NT_ALIGN_CENTER, nt_pcol(w));
    snprintf(s, sizeof(s), "%d", m->score[0]);
    nt_txt(g, f->poster, s, NTG_W / 2 - 18, 252, NT_ALIGN_RIGHT, NT_COL_P1);
    nt_txt(g, f->poster, ":", NTG_W / 2, 250, NT_ALIGN_CENTER, NT_COL_MUTED);
    snprintf(s, sizeof(s), "%d", m->score[1]);
    nt_txt(g, f->poster, s, NTG_W / 2 + 18, 252, NT_ALIGN_LEFT, NT_COL_P2);
    nt_choices(g, f, 292, "再来一场", "回封面", m->choice, nt_pcol(w));
}

static void scene_pause(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    static const char *const ITEMS[2] = {"继续", "回到封面"};
    ntg_rect(g, 0, 0, NTG_W, NTG_H, 0, NT_COL_WALL, 255, 0, 0);   // opaque: nothing private shows through
    nt_txt(g, f->f24, "暂停", NTG_W / 2, 118, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
    for (int i = 0; i < 2; i++) {
        int y = 158 + i * 30;
        bool sel = m->pause_cursor == i;
        if (sel) ntg_rect(g, 64, y - 12, 112, 24, 12, NT_COL_BRASS, 255, 0, 0);
        nt_txt(g, f->f16, ITEMS[i], NTG_W / 2, y, NT_ALIGN_CENTER, sel ? NT_COL_INK : NT_COL_FELT_TEXT);
    }
    nt_hint(g, f, "↑↓ 选择 · OK 确认");
}

void nt_scene_build(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    g->font_small = f->f12;
    g->font_ui = f->f16;
    g->font_mono = f->mono;
    g->font_poster = f->poster;
    ntg_begin(g, m->paused ? 100 : (int)m->scene);
    if (m->paused) { scene_pause(g, m, f); return; }
    switch (m->scene) {
    case NT_MENU: scene_menu(g, m, f); break;
    case NT_HANDOFF: scene_handoff(g, m, f); break;
    case NT_BJ_DEAL:
    case NT_BJ_REVEAL: nt_scene_bj(g, m, f); break;
    case NT_BJ_TURN: nt_scene_bj_turn(g, m, f); break;
    case NT_DICE_ROLL:
    case NT_DICE_REVEAL: nt_scene_dice(g, m, f); break;
    case NT_DICE_TURN: nt_scene_dice_turn(g, m, f); break;
    case NT_END: scene_end(g, m, f); break;
    }
}
