// Who is the undercover screens: setup (player count), hand-off, the private word, the
// describing round, the vote on the badge, who was voted out, and the final reveal.
// Words are drawn with the 24 px font, which carries every character of the word list.
#include <stdio.h>

#include "nabo_table_scene_ui.h"
#include "nabo_uc_words.h"

#define UC_COL_CARD ntg_rgb(244, 241, 232)
#define UC_COL_DIM ntg_rgb(118, 148, 138)

// A player's name tag in the 2 x 4 grid: coloured name, or dimmed with "出局" once out.
// sel fills the tag with the player's colour. tag (optional) sits on the right, e.g. "先" or "卧底".
static void chip(ntg_t *g, const nt_fonts_t *f, int x, int y, int p, bool out, bool sel, const char *tag,
                 uint16_t tag_col)
{
    uint16_t col = nt_pcol(p);
    if (sel) ntg_rect(g, x, y - 10, 104, 20, 10, col, 255, 0, 0);
    else ntg_rect(g, x, y - 10, 104, 20, 10, ntg_rgb(255, 255, 255), out ? 6 : 14, out ? 0 : col, out ? 0 : 1);
    nt_txt(g, f->f12, NT_PNAME[p], x + 10, y, NT_ALIGN_LEFT, sel ? NT_COL_INK : out ? UC_COL_DIM : col);
    if (out && !tag) nt_txt(g, f->f12, "出局", x + 94, y, NT_ALIGN_RIGHT, UC_COL_DIM);
    if (tag) nt_txt(g, f->f12, tag, x + 94, y, NT_ALIGN_RIGHT, sel ? NT_COL_INK : tag_col);
}

static void chip_xy(int i, int y0, int step, int *x, int *y)
{
    *x = i & 1 ? 124 : 12;
    *y = y0 + (i >> 1) * step;
}

// ---------- setup ----------

static void scene_setup(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_uc_t *u = &m->uc;
    char s[32];
    ntg_bg(g, 168);
    nt_dealer(g, m, 168, true);
    nt_txt(g, f->f24, "谁是卧底", NTG_W / 2, 191, NT_ALIGN_CENTER, NT_COL_BRASS);
    nt_txt(g, f->f12, "几个人玩？", NTG_W / 2, 214, NT_ALIGN_CENTER, NT_COL_MUTED);
    ntg_rect(g, 74, 228, 56, 40, 10, NT_COL_BRASS, 255, 0, 0);
    snprintf(s, sizeof(s), "%d", u->cursor);
    nt_txt(g, f->poster, s, 102, 249, NT_ALIGN_CENTER, NT_COL_INK);
    nt_txt(g, f->f18, "人", 140, 248, NT_ALIGN_LEFT, NT_COL_FELT_TEXT);
    if (u->cursor < NT_UC_MAX) ntg_tri(g, 102, 221, 6, true, NT_COL_FELT_TEXT);
    if (u->cursor > NT_UC_MIN) ntg_tri(g, 102, 275, 6, false, NT_COL_FELT_TEXT);
    snprintf(s, sizeof(s), "%d 个卧底", u->cursor >= 7 ? 2 : 1);
    nt_txt(g, f->f12, s, 182, 248, NT_ALIGN_LEFT, NT_COL_MUTED);
    nt_hint(g, f, "↑↓ 人数 · OK 发词 · 长按 OK 返回");
}

// ---------- hand-off and the private word ----------

void nt_scene_uc_handoff(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int p = m->handoff_to;
    char s[48];
    ntg_bg(g, 170);
    nt_dealer(g, m, 170, true);
    nt_txt(g, f->f12, "请交给", NTG_W / 2, 194, NT_ALIGN_CENTER, NT_COL_MUTED);
    nt_txt(g, f->f24, NT_PNAME[p], NTG_W / 2, 220, NT_ALIGN_CENTER, nt_pcol(p));
    if (m->uc.recheck) snprintf(s, sizeof(s), "再看一眼自己的词");
    else snprintf(s, sizeof(s), "看自己的词（第 %d / %d 位）", p + 1, m->uc.n);
    nt_txt(g, f->f12, s, NTG_W / 2, 250, NT_ALIGN_CENTER, NT_COL_MUTED);
    ntg_rect(g, 48, 274, 144, 26, 13, nt_pcol(p), 255, 0, 0);
    nt_txt(g, f->f16, "准备好按 OK", NTG_W / 2, 287, NT_ALIGN_CENTER, NT_COL_INK);
    nt_txt(g, f->f12, "别让别人看到屏幕 · 长按 OK 暂停", NTG_W / 2, 311, NT_ALIGN_CENTER, NT_COL_MUTED);
}

static void scene_word(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    int p = m->turn;
    char s[48];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    snprintf(s, sizeof(s), "%s · 你的词", NT_PNAME[p]);
    nt_chip(g, f, p, s);
    ntg_rect(g, 30, 120, 180, 76, 12, UC_COL_CARD, 255, nt_pcol(p), 2);
    nt_txt(g, f->f24, nt_uc_word(m, p), NTG_W / 2, 158, NT_ALIGN_CENTER, NT_COL_INK);
    nt_txt(g, f->f12, "你可能是卧底，自己并不知道", NTG_W / 2, 220, NT_ALIGN_CENTER, NT_COL_MUTED);
    nt_txt(g, f->f12, "描述时别说得太直白", NTG_W / 2, 238, NT_ALIGN_CENTER, NT_COL_MUTED);
    if (m->uc.recheck) snprintf(s, sizeof(s), "记住了按 OK 盖上");
    else if (p + 1 < m->uc.n) snprintf(s, sizeof(s), "OK 盖上，交给%s", NT_PNAME[p + 1]);
    else snprintf(s, sizeof(s), "OK 盖上，开始描述");
    nt_hint(g, f, s);
}

// ---------- describing round ----------

static void title_bar(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    char s[32];
    snprintf(s, sizeof(s), "谁是卧底 · 第 %d 轮", m->uc.round);
    nt_txt(g, f->f12, s, NTG_W / 2, 9, NT_ALIGN_CENTER, NT_COL_MUTED);
    snprintf(s, sizeof(s), "剩 %d 人", nt_uc_alive(m, false) + nt_uc_alive(m, true));
    nt_txt(g, f->f12, s, 8, 9, NT_ALIGN_LEFT, NT_COL_MUTED);
}

static void scene_talk(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_uc_t *u = &m->uc;
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    title_bar(g, m, f);
    nt_txt(g, f->f18, "轮流描述自己的词", NTG_W / 2, 112, NT_ALIGN_CENTER, NT_COL_FELT_TEXT);
    nt_seg_t run[3] = {{"从 ", NT_COL_MUTED}, {NT_PNAME[u->speaker], nt_pcol(u->speaker)}, {" 开始，每人说一句", NT_COL_MUTED}};
    nt_runs(g, f->f12, NTG_W / 2, 134, run, 3, NT_ALIGN_CENTER);
    for (int p = 0; p < u->n; p++) {
        int x, y;
        chip_xy(p, 160, 24, &x, &y);
        chip(g, f, x, y, p, !u->alive[p], false, p == u->speaker ? "先" : NULL, NT_COL_BRASS);
    }
    nt_choices(g, f, 300, "开始投票", "有人忘词", u->cursor, NT_COL_BRASS);
}

// ---------- vote ----------

static void scene_vote(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_uc_t *u = &m->uc;
    bool forgot = u->list == NT_UC_LIST_FORGOT;
    int len = nt_uc_list_len(m);
    char s[48];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    title_bar(g, m, f);
    nt_txt(g, f->f18, forgot ? "谁忘了自己的词？" : "大家商量好，投谁出局？", NTG_W / 2, 112, NT_ALIGN_CENTER,
           NT_COL_FELT_TEXT);
    for (int row = 0; row < len - 1; row++) {
        int x, y;
        chip_xy(row, 138, 24, &x, &y);
        chip(g, f, x, y, nt_uc_list_player(m, row), false, row == u->cursor, NULL, 0);
    }
    bool last = u->cursor == len - 1;
    int by = 138 + ((len - 1 + 1) / 2) * 24 + 6;
    ntg_rect(g, 60, by - 11, 120, 22, 11, last ? NT_COL_BRASS : ntg_rgb(255, 255, 255), last ? 255 : 16, 0, 0);
    nt_txt(g, f->f16, forgot ? "返回" : "平票，再说一轮", NTG_W / 2, by, NT_ALIGN_CENTER,
           last ? NT_COL_INK : NT_COL_FELT_TEXT);
    if (!u->confirm) {
        nt_hint(g, f, forgot ? "↑↓ 选人 · OK 交给他看词" : "↑↓ 选人 · OK 确定");
        return;
    }
    int p = nt_uc_list_player(m, u->cursor);
    ntg_rect(g, 0, 266, NTG_W, 54, 0, ntg_rgb(4, 12, 10), 235, 0, 0);
    ntg_rect(g, 0, 266, NTG_W, 2, 0, nt_pcol(p), 255, 0, 0);
    snprintf(s, sizeof(s), "投出 %s？", NT_PNAME[p]);
    nt_txt(g, f->f16, s, NTG_W / 2, 282, NT_ALIGN_CENTER, nt_pcol(p));
    nt_choices(g, f, 304, "确定出局", "取消", u->confirm_cursor, nt_pcol(p));
}

// ---------- voted out ----------

static void scene_out(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_uc_t *u = &m->uc;
    int p = u->out;
    char s[48];
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    title_bar(g, m, f);
    snprintf(s, sizeof(s), "%s 出局", NT_PNAME[p]);
    nt_txt(g, f->f24, s, NTG_W / 2, 128, NT_ALIGN_CENTER, nt_pcol(p));
    nt_txt(g, f->f24, u->uc[p] ? "是卧底！" : "是平民", NTG_W / 2, 166, NT_ALIGN_CENTER,
           u->uc[p] ? NT_COL_RED : NT_COL_FELT_TEXT);
    int und = nt_uc_alive(m, true), all = und + nt_uc_alive(m, false);
    if (m->result.has) {
        nt_txt(g, f->f18, u->uc_won ? "卧底撑到了最后" : "卧底全部出局", NTG_W / 2, 214, NT_ALIGN_CENTER, NT_COL_BRASS);
        nt_hint(g, f, "OK 看结果");
        return;
    }
    snprintf(s, sizeof(s), "还剩 %d 人，卧底还有 %d 个", all, und);
    nt_txt(g, f->f12, s, NTG_W / 2, 210, NT_ALIGN_CENTER, NT_COL_MUTED);
    nt_hint(g, f, "OK 继续描述下一轮");
}

// ---------- final reveal ----------

static void scene_end(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    const nt_uc_t *u = &m->uc;
    int civ = u->swap ? 1 : 0;
    ntg_bg(g, NT_PLAY_RIM);
    nt_dealer(g, m, NT_PLAY_RIM, false);
    nt_txt(g, f->f12, "谁是卧底 · 结果", NTG_W / 2, 9, NT_ALIGN_CENTER, NT_COL_MUTED);
    nt_txt(g, f->f24, u->uc_won ? "卧底胜利" : "平民胜利", NTG_W / 2, 112, NT_ALIGN_CENTER,
           u->uc_won ? NT_COL_RED : NT_COL_BRASS);
    nt_txt(g, f->f12, "平民词", 22, 144, NT_ALIGN_LEFT, NT_COL_MUTED);
    nt_txt(g, f->f24, NT_UC_WORDS[u->pair][civ], 64, 144, NT_ALIGN_LEFT, NT_COL_FELT_TEXT);
    nt_txt(g, f->f12, "卧底词", 22, 172, NT_ALIGN_LEFT, NT_COL_MUTED);
    nt_txt(g, f->f24, NT_UC_WORDS[u->pair][1 - civ], 64, 172, NT_ALIGN_LEFT, NT_COL_RED);
    for (int p = 0; p < u->n; p++) {
        int x, y;
        chip_xy(p, 198, 22, &x, &y);
        chip(g, f, x, y, p, !u->alive[p], false, u->uc[p] ? "卧底" : !u->alive[p] ? "出局" : NULL, NT_COL_RED);
    }
    nt_choices(g, f, 304, "再来一局", "回封面", m->choice, u->uc_won ? NT_COL_RED : NT_COL_BRASS);
}

void nt_scene_uc(ntg_t *g, const nt_t *m, const nt_fonts_t *f)
{
    switch (m->scene) {
    case NT_UC_SETUP: scene_setup(g, m, f); break;
    case NT_UC_WORD: scene_word(g, m, f); break;
    case NT_UC_TALK: scene_talk(g, m, f); break;
    case NT_UC_VOTE: scene_vote(g, m, f); break;
    case NT_UC_OUT: scene_out(g, m, f); break;
    case NT_UC_END: scene_end(g, m, f); break;
    default: break;
    }
}
