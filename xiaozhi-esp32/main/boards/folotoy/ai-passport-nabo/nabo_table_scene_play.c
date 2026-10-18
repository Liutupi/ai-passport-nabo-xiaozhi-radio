// NABO TABLE game tables: 21 points (deal, private turn, reveal) and liar's dice (roll,
// private turn with the two-step bid picker, reveal). Coordinates follow the prototype.
#include "nabo_table_scene_ui.h"

#include <stdio.h>

static const char *const SUIT_NAME[4] = {"黑桃", "红心", "梅花", "方块"};
static const char *const RANK_NAME[14] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

// |cos(pi * f)| * 64 for f in 0..256, from a 17-entry table.
static int flip_squash(int f)
{
    static const uint8_t COS[17] = {64, 63, 59, 53, 45, 36, 24, 12, 0, 12, 24, 36, 45, 53, 59, 63, 64};
    f = f < 0 ? 0 : f > 256 ? 256 : f;
    int i = f / 16, r = f % 16;
    return i >= 16 ? COS[16] : COS[i] + (COS[i + 1] - COS[i]) * r / 16;
}

static int clamp256(double v) { return v <= 0 ? 0 : v >= 1 ? 256 : (int)(v * 256); }

// ---------- 21 points ----------

static const int BJ_COL_X[2] = {8, 124};
#define BJ_TOP 112

static void bj_slot(int p, int i, int n, int *x, int *y)
{
    int dy = n > 1 ? (88 / (n - 1) < 20 ? 88 / (n - 1) : 20) : 0;
    *x = BJ_COL_X[p] + 18 + (i % 2) * 6;
    *y = BJ_TOP + 16 + i * dy;
}

void nt_scene_bj(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    bool reveal = m->scene == NT_BJ_REVEAL;
    char s[64];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    nt_scores(g, m, f);
    for (int p = 0; p < 2; p++) {
        int n = m->bj.hand_n[p];
        nt_txt(g, f->f12, NT_PNAME[p], BJ_COL_X[p] + 4, BJ_TOP, NT_ALIGN_LEFT, nt_pcol(p));
        if (m->bj.stood[p]) snprintf(s, sizeof(s), "已停牌");
        else snprintf(s, sizeof(s), "%d 张", n);
        nt_txt(g, f->f12, s, BJ_COL_X[p] + 104, BJ_TOP, NT_ALIGN_RIGHT, NT_COL_MUTED);
        for (int i = 0; i < n; i++) {
            const nt_card_t *c = &m->bj.hand[p][i];
            int x, y;
            bj_slot(p, i, n, &x, &y);
            double age = m->clock - c->born;
            if (age < 0) continue;
            if (age < 0.3) {   // flying out of NABO's hands
                int e = nt_ease_out(clamp256(age / 0.3));
                ntg_card(g, 111 + (x - 111) * e / 256, 70 + (y - 70) * e / 256, 36, 50, 0, 0, false, 64);
                continue;
            }
            int fl = reveal ? (int)(nt_bj_flip(m, p, i) * 256) : 0;
            ntg_card(g, x, y, 36, 50, c->r, c->s, fl >= 128, flip_squash(fl));
        }
    }
    if (reveal && m->result.has) {
        for (int p = 0; p < 2; p++) {
            const nt_total_t *t = &m->result.bj[p];
            if (t->bust) {
                nt_txt(g, f->f18, "爆", BJ_COL_X[p] + 104, 150, NT_ALIGN_RIGHT, NT_COL_RED);
            } else {
                snprintf(s, sizeof(s), "%d", t->total);
                nt_txt(g, f->poster, s, BJ_COL_X[p] + 104, 150, NT_ALIGN_RIGHT, NT_COL_FELT_TEXT);
            }
        }
        const nt_total_t *a = &m->result.bj[0], *b = &m->result.bj[1];
        char x[8], y[8];
        snprintf(x, sizeof(x), a->bust ? "%d 爆" : "%d", a->total);
        snprintf(y, sizeof(y), b->bust ? "%d 爆" : "%d", b->total);
        if (a->bust && b->bust) snprintf(s, sizeof(s), "双方爆牌（%s 对 %s）", x, y);
        else if (m->result.winner < 0) snprintf(s, sizeof(s), "点数相同（%s 对 %s）", x, y);
        else snprintf(s, sizeof(s), "%s 对 %s", x, y);
        nt_result_banner(g, m, f, s);
    } else if (m->scene == NT_BJ_DEAL) {
        nt_hint(g, f, "NABO 正在发牌");
    }
}

void nt_scene_bj_turn(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int p = m->turn, o = 1 - p, n = m->bj.hand_n[p];
    const nt_card_t *hand = m->bj.hand[p];
    nt_total_t ht = nt_hand_total(hand, n);
    char s[64];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    nt_scores(g, m, f);
    snprintf(s, sizeof(s), "%s · 你的牌", NT_PNAME[p]);
    nt_chip(g, f, p, s);
    const int cw = 44, ch = 62;
    int step = n > 1 ? ((NTG_W - 32 - cw) / (n - 1) < 50 ? (NTG_W - 32 - cw) / (n - 1) : 50) : 0;
    int x0 = (NTG_W - (cw + step * (n - 1))) / 2;
    for (int i = 0; i < n; i++) {
        double age = m->clock - hand[i].born;
        int x = x0 + i * step, y = 106;
        if (age < 0.45) {   // the card just drawn turns over as it lands
            int k = clamp256(age / 0.45), fl = clamp256((age - 0.1) / 0.35);
            ntg_card(g, x, y - 10 * (256 - k) / 256, cw, ch, hand[i].r, hand[i].s, fl >= 128, flip_squash(fl));
        } else {
            ntg_card(g, x, y, cw, ch, hand[i].r, hand[i].s, true, 64);
        }
    }
    snprintf(s, sizeof(s), "%d", ht.total);
    int w = ntg_text_width(f->poster, s);
    nt_txt(g, f->poster, s, 18, 194, NT_ALIGN_LEFT, ht.bust ? NT_COL_RED : NT_COL_FELT_TEXT);
    nt_txt(g, f->f16, "点", 24 + w, 199, NT_ALIGN_LEFT, NT_COL_MUTED);
    if (ht.bust) {
        nt_txt(g, f->f16, "爆牌了，只能停牌", 46 + w, 199, NT_ALIGN_LEFT, NT_COL_RED);
    } else if (ht.soft) {
        snprintf(s, sizeof(s), "A 记 11（也可看作 %d）", ht.hard);
        nt_txt(g, f->f12, s, 46 + w, 199, NT_ALIGN_LEFT, NT_COL_MUTED);
    }
    snprintf(s, sizeof(s), "%s：%d 张%s", NT_PNAME[o], m->bj.hand_n[o], m->bj.stood[o] ? " · 已停牌" : "");
    nt_txt(g, f->f16, s, 18, 228, NT_ALIGN_LEFT, nt_pcol(o));
    if (m->bj.phase == NT_BJ_HIT_SHOW) {
        const nt_card_t *c = &hand[n - 1];
        snprintf(s, sizeof(s), "拿到 %s %s", SUIT_NAME[c->s], RANK_NAME[c->r]);
        nt_txt(g, f->f18, s, NTG_W / 2, 258, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
        snprintf(s, sizeof(s), m->bj.stood[o] ? "OK 继续 · 长按 OK 暂停" : "OK 盖牌交给%s · 长按 OK 暂停", NT_PNAME[o]);
        nt_hint(g, f, s);
        return;
    }
    static const char *const LABEL[2] = {"要牌", "停牌"};
    for (int i = 0; i < 2; i++) {
        int x = 18 + i * 108;
        bool sel = m->bj.cursor == i, off = i == 0 && ht.bust;
        ntg_rect(g, x, 244, 96, 36, 8, sel ? nt_pcol(p) : ntg_rgb(255, 255, 255), sel ? 255 : 13,
                 sel ? nt_pcol(p) : ntg_rgb(52, 82, 74), 1);
        nt_txt(g, f->f18, LABEL[i], x + 48, 262, NT_ALIGN_CENTER,
               off ? ntg_rgb(60, 86, 80) : sel ? NT_COL_INK : NT_COL_FELT_TEXT);
    }
    nt_hint(g, f, "↑↓ 选择 · OK 确认 · 长按 OK 暂停");
}

// ---------- liar's dice ----------

static const int DC_COL_X[2] = {62, 178};

// A row (or a 3 + 2 / 3 + 3 block when cols) of dice; size is the prototype's box size.
static void dice_row(ntg_t *g, const nt_t *m, int p, int cx, int y, int size, bool cols, int glow_face, bool wild)
{
    const nt_dice_t *d = &m->dc;
    int gap = 6, per = d->per, row_n = per > 3 && cols ? (per + 1) / 2 : per;
    for (int i = 0; i < per; i++) {
        int r = i / row_n, c = i % row_n, n = per - r * row_n < row_n ? per - r * row_n : row_n;
        int x = cx - (n * size + (n - 1) * gap) / 2 + c * (size + gap), top = y + r * (size + gap);
        int v = d->dice[p][i];
        bool counts = glow_face > 0 && (v == glow_face || (wild && v == 1));
        if (counts) ntg_glow(g, x + size / 2, top + size / 2, size * 7 / 10, NT_COL_BRASS);
        ntg_die(g, v, x + size / 2, top + size * 3 / 4, size * 10 / 27, (int)d->yaw[p][i], glow_face > 0 && !counts);
    }
}

void nt_scene_dice(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    bool reveal = m->scene == NT_DICE_REVEAL;
    const nt_bid_t *last = nt_last_bid(m);
    char s[64];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    nt_scores(g, m, f);
    int lift = reveal ? nt_ease_out(clamp256((m->t - 0.45) / 0.35)) : 0;
    bool hl = reveal && m->t > 0.9;
    for (int p = 0; p < 2; p++) {
        int cx = DC_COL_X[p];
        nt_txt(g, f->f12, NT_PNAME[p], cx, BJ_TOP, NT_ALIGN_CENTER, nt_pcol(p));
        if (m->scene == NT_DICE_ROLL) {
            int land = clamp256((m->t - 1.25) / 0.2);
            if (land > 0) ntg_cup(g, cx, 190 - 8 * (256 - land) / 256, 255);
            continue;
        }
        if (reveal && last) dice_row(g, m, p, cx, 138, 24, true, hl ? last->f : 0, nt_wild_counts(m, last->f));
        if (lift < 256) ntg_cup(g, cx, 190 - 70 * lift / 256, 255 - lift * 255 / 256);
    }
    if (last) {
        snprintf(s, sizeof(s), "%s 叫", NT_PNAME[last->p]);
        nt_txt(g, f->f12, s, NTG_W / 2, 204, NT_ALIGN_CENTER, nt_pcol(last->p));
        nt_bid_label(g, f, NTG_W / 2, 227, last->q, last->f, NT_ALIGN_CENTER, NT_COL_FELT_TEXT, 28);
        if (hl) {
            snprintf(s, sizeof(s), "实际 %d 个%s", nt_count_face(m, last->f), nt_wild_counts(m, last->f) ? "（含万能 1）" : "");
            nt_txt(g, f->f16, s, NTG_W / 2, 254, NT_ALIGN_CENTER, NT_COL_BRASS);
        }
    }
    if (reveal && m->result.has) {
        if (m->result.bid_held) snprintf(s, sizeof(s), "叫中了：实际 %d 个 ≥ %d 个", m->result.actual, m->result.bid_q);
        else snprintf(s, sizeof(s), "开中了：实际只有 %d 个", m->result.actual);
        nt_result_banner(g, m, f, s);
    } else if (m->scene == NT_DICE_ROLL) {
        nt_hint(g, f, "NABO 正在摇盅");
    }
}

static void picker(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_dice_t *d = &m->dc;
    uint16_t col = nt_pcol(m->turn);
    const int cy = 222;
    char s[48];
    int qv = d->qlist[d->qi];
    if (d->step == NT_STEP_QTY && qv == 0) {   // "open"
        ntg_rect(g, 60, cy - 20, 120, 40, 12, NT_COL_BRASS, 255, 0, 0);
        nt_txt(g, f->f24, "开！", NTG_W / 2, cy, NT_ALIGN_CENTER, NT_COL_INK);
        if (d->qn > 1) ntg_tri(g, NTG_W / 2, cy - 30, 7, true, NT_COL_FELT_TEXT);
        nt_txt(g, f->f12, "按 ↑ 改为加注", NTG_W / 2, 270, NT_ALIGN_CENTER, NT_COL_MUTED);
        nt_hint(g, f, "↑↓ 选数量 · OK 确认 · 长按 OK 暂停");
        return;
    }
    bool qa = d->step == NT_STEP_QTY;
    int fv = qa ? -1 : d->flist[d->fi];
    ntg_rect(g, 50, cy - 22, 56, 44, 10, qa ? col : ntg_rgb(255, 255, 255), qa ? 255 : 16, 0, 0);
    snprintf(s, sizeof(s), "%d", qv);
    nt_txt(g, f->poster, s, 78, cy + 1, NT_ALIGN_CENTER, qa ? NT_COL_INK : NT_COL_FELT_TEXT);
    nt_txt(g, f->f16, "个", NTG_W / 2, cy, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
    ntg_rect(g, 134, cy - 22, 56, 44, 10, !qa ? col : ntg_rgb(255, 255, 255), !qa ? 255 : 16, 0, 0);
    if (qa) {
        nt_txt(g, f->poster, "?", 162, cy + 1, NT_ALIGN_CENTER, NT_COL_MUTED);
    } else if (fv == 0) {
        nt_txt(g, f->f16, "返回", 162, cy, NT_ALIGN_CENTER, NT_COL_INK);
    } else {
        ntg_die(g, fv, 162, cy + 10, 12, 24, false);
        if (fv == 1 && d->wild) nt_txt(g, f->f12, "斋", 200, cy, NT_ALIGN_LEFT, NT_COL_BRASS);
    }
    int ax = qa ? 78 : 162, at = qa ? d->qi : d->fi, len = qa ? d->qn : d->fn;
    if (at < len - 1) ntg_tri(g, ax, cy - 31, 7, true, NT_COL_FELT_TEXT);
    if (at > 0) ntg_tri(g, ax, cy + 31, 7, false, NT_COL_FELT_TEXT);
    if (qa) {
        if (d->qi > 0 && d->qlist[0] == 0) nt_txt(g, f->f12, "按到底是「开！」", NTG_W / 2, 270, NT_ALIGN_CENTER, NT_COL_MUTED);
    } else if (fv == 0) {
        nt_txt(g, f->f12, "OK 回去改数量", NTG_W / 2, 270, NT_ALIGN_CENTER, NT_COL_MUTED);
    } else {
        snprintf(s, sizeof(s), "OK 叫出「%d 个 %d」", qv, fv);
        nt_txt(g, f->f12, s, NTG_W / 2, 270, NT_ALIGN_CENTER, NT_COL_MUTED);
    }
    nt_hint(g, f, qa ? "↑↓ 选数量 · OK 下一步 · 长按 OK 暂停" : "↑↓ 选点数 · OK 确认 · 长按 OK 暂停");
}

void nt_scene_dice_turn(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int p = m->turn;
    const nt_bid_t *last = nt_last_bid(m);
    char s[48];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    nt_scores(g, m, f);
    snprintf(s, sizeof(s), "%s · 你的骰子", NT_PNAME[p]);
    nt_chip(g, f, p, s);
    dice_row(g, m, p, NTG_W / 2, 104, 30, false, 0, false);
    if (!last) {
        nt_txt(g, f->f16, "还没人叫，你先叫", NTG_W / 2, 160, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
    } else {
        snprintf(s, sizeof(s), "%s 叫", NT_PNAME[last->p]);
        nt_txt(g, f->f12, s, NTG_W / 2, 147, NT_ALIGN_CENTER, nt_pcol(last->p));
        int w = nt_bid_label(g, f, NTG_W / 2, 169, last->q, last->f, NT_ALIGN_CENTER, NT_COL_FELT_TEXT, 30);
        if (nt_zhai(m)) nt_txt(g, f->f16, "斋", NTG_W / 2 + w / 2 + 8, 169, NT_ALIGN_LEFT, NT_COL_BRASS);
    }
    picker(g, m, f);
}
