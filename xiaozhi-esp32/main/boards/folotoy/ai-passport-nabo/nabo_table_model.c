// NABO TABLE rules: menu, hand-off, pause, match flow and 21 points. Liar's dice is in
// nabo_table_bid.c. Times are doubles so a host replay matches the JavaScript prototype.
#include "nabo_table_model.h"

#include <math.h>
#include <string.h>

// One-shot animation defaults (seconds), as in the prototype's placeholder NABO.
static const float ANIM_DEFAULT[NT_ANIM_COUNT] = {2, 0.9f, 0.33f, 0.5f, 0.25f, 0.25f, 1.4f, 0.45f, 0.35f, 0.4f, 1, 0.4f};
static const bool ANIM_LOOP[NT_ANIM_COUNT] = {1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0};
static const bool ANIM_HOLD[NT_ANIM_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1};
static const nt_anim_t MENU_ANIM[NT_MENU_COUNT] = {NT_ANIM_SHUFFLE, NT_ANIM_SHAKE, NT_ANIM_IDLE};

#define DEAL_GAP 0.35        // seconds between dealt cards
#define BJ_FLIP0 0.5         // reveal: first card starts turning
#define BJ_FLIP_STEP 0.12

// ---------- random numbers: mulberry32, bit-identical to the prototype ----------

double nt_rand(nt_t *g)
{
    g->rng += 0x6D2B79F5u;
    uint32_t t = g->rng;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double)(t ^ (t >> 14)) / 4294967296.0;
}

static int rand_int(nt_t *g, int n) { return (int)floor(nt_rand(g) * n); }

// ---------- animation and scene plumbing ----------

float nt_anim_len(const nt_t *g, nt_anim_t a)
{
    return g->cfg.anim_s[a] > 0 ? g->cfg.anim_s[a] : ANIM_DEFAULT[a];
}

void nt_play(nt_t *g, nt_anim_t a, bool mirror)
{
    g->anim = a;
    g->anim_t = 0;
    g->mirror = mirror;
    g->has_next = a == NT_ANIM_COVER_IN;
    g->anim_next = NT_ANIM_COVER;
}

static void anim_update(nt_t *g, double dt)
{
    g->anim_t += dt;
    if (ANIM_LOOP[g->anim] || ANIM_HOLD[g->anim] || g->anim_t < nt_anim_len(g, g->anim)) return;
    nt_play(g, g->has_next ? g->anim_next : NT_ANIM_IDLE, false);
}

void nt_go(nt_t *g, nt_scene_t scene)
{
    g->scene = scene;
    g->t = 0;
    g->sfx_mark = 0;
}

static void menu_select(nt_t *g, int i)
{
    g->menu = (uint8_t)i;
    if (g->anim != MENU_ANIM[i]) nt_play(g, MENU_ANIM[i], false);
}

static void to_menu(nt_t *g)
{
    g->game = NT_GAME_NONE;
    nt_go(g, NT_MENU);
    menu_select(g, g->menu == NT_MENU_TARGET ? 0 : g->menu);
}

void nt_handoff(nt_t *g, int p)
{
    g->handoff_to = (uint8_t)p;
    nt_go(g, NT_HANDOFF);
    nt_play(g, NT_ANIM_COVER_IN, false);
}

static void begin_turn(nt_t *g, int p)
{
    g->turn = (uint8_t)p;
    if (g->game == NT_GAME_BJ) {
        g->bj.phase = NT_BJ_CHOOSE;
        g->bj.cursor = 0;
        nt_go(g, NT_BJ_TURN);
    } else {
        nt_dice_build_bids(g);
        nt_go(g, NT_DICE_TURN);
    }
    nt_play(g, NT_ANIM_IDLE, false);
}

// ---------- match flow ----------

static void next_round(nt_t *g)
{
    g->round++;
    memset(&g->result, 0, sizeof(g->result));
    if (g->game == NT_GAME_BJ) {
        g->starter = (uint8_t)((g->round - 1) % 2);
        nt_bj_t *b = &g->bj;
        memset(b, 0, sizeof(*b));
        for (int s = 0, k = 0; s < 4; s++)
            for (int r = 1; r <= 13; r++, k++) { b->deck[k][0] = (uint8_t)r; b->deck[k][1] = (uint8_t)s; }
        for (int i = 51; i > 0; i--) {
            int j = rand_int(g, i + 1);
            uint8_t r = b->deck[i][0], s = b->deck[i][1];
            b->deck[i][0] = b->deck[j][0]; b->deck[i][1] = b->deck[j][1];
            b->deck[j][0] = r; b->deck[j][1] = s;
        }
        b->deck_n = 52;
        nt_go(g, NT_BJ_DEAL);
        nt_play(g, NT_ANIM_SHUFFLE, false);
        g->events |= NT_EV_SHUFFLE;
    } else {
        g->starter = g->round == 1 ? 0 : g->loser;
        nt_dice_new_round(g);
        nt_go(g, NT_DICE_ROLL);
        nt_play(g, NT_ANIM_SHAKE, false);
        g->events |= NT_EV_RATTLE;
    }
}

static void start_match(nt_t *g, nt_game_t game)
{
    g->game = game;
    g->score[0] = g->score[1] = 0;
    g->round = 0;
    g->loser = 0;
    next_round(g);
}

void nt_win_round(nt_t *g, int winner)
{
    g->result.has = true;
    g->result.winner = (int8_t)winner;
    g->choice = 0;
    if (winner < 0) { nt_play(g, NT_ANIM_SHRUG, false); return; }
    g->score[winner]++;
    g->loser = (uint8_t)(1 - winner);
    nt_play(g, NT_ANIM_POINT, winner == 1);
    g->events |= NT_EV_WIN;
}

bool nt_match_over(const nt_t *g)
{
    return g->result.has && g->result.winner >= 0 && g->score[g->result.winner] >= g->cfg.target;
}

static void after_result(nt_t *g)
{
    if (g->choice == 1) { to_menu(g); return; }
    if (nt_match_over(g)) {
        g->choice = 0;
        nt_go(g, NT_END);
        nt_play(g, NT_ANIM_TROPHY, false);
    } else {
        next_round(g);
    }
}

// ---------- 21 points ----------

nt_total_t nt_hand_total(const nt_card_t *hand, int n)
{
    int t = 0;
    bool ace = false;
    for (int i = 0; i < n; i++) {
        t += hand[i].r < 10 ? hand[i].r : 10;
        if (hand[i].r == 1) ace = true;
    }
    nt_total_t out = {t, t, ace && t + 10 <= 21, t > 21};
    if (out.soft) out.total = t + 10;
    return out;
}

static void draw_card(nt_t *g, int p)
{
    nt_bj_t *b = &g->bj;
    b->deck_n--;
    nt_card_t c = {b->deck[b->deck_n][0], b->deck[b->deck_n][1], g->clock};
    b->hand[p][b->hand_n[p]++] = c;
}

static void bj_deal_update(nt_t *g)
{
    nt_bj_t *b = &g->bj;
    const int order[4] = {g->starter, 1 - g->starter, g->starter, 1 - g->starter};
    double t0 = fmax(0.8, nt_anim_len(g, NT_ANIM_SHUFFLE));   // finish the cut, then deal
    int due = (int)floor((g->t - t0) / DEAL_GAP) + 1;
    while (b->dealt < (due < 4 ? due : 4)) {
        draw_card(g, order[b->dealt++]);
        nt_play(g, NT_ANIM_DEAL, false);
        g->events |= NT_EV_CARD;
    }
    if (b->dealt == 4 && g->t > t0 + 4 * DEAL_GAP + DEAL_GAP) nt_handoff(g, g->starter);
}

static void bj_end_turn(nt_t *g)
{
    nt_bj_t *b = &g->bj;
    int o = 1 - g->turn;
    if (b->stood[0] && b->stood[1]) {
        nt_go(g, NT_BJ_REVEAL);
        nt_play(g, NT_ANIM_OPEN, false);
        g->events |= NT_EV_OPEN;
    } else if (!b->stood[o]) {
        nt_handoff(g, o);
    } else {
        b->phase = NT_BJ_CHOOSE;   // the other player has stood: keep going without a hand-off
        b->cursor = 0;
    }
}

static void bj_key(nt_t *g, nt_key_t k)
{
    nt_bj_t *b = &g->bj;
    int p = g->turn;
    bool bust = nt_hand_total(b->hand[p], b->hand_n[p]).bust;
    if (b->phase == NT_BJ_HIT_SHOW) {
        if (k == NT_KEY_OK) { g->events |= NT_EV_OK; bj_end_turn(g); }
        return;
    }
    if (k == NT_KEY_UP || k == NT_KEY_DOWN) {
        if (!bust) { b->cursor ^= 1; g->events |= NT_EV_MOVE; }
        return;
    }
    if (k != NT_KEY_OK) return;
    g->events |= NT_EV_OK;
    if (b->cursor == 0 && !bust) {
        draw_card(g, p);
        b->phase = NT_BJ_HIT_SHOW;
        g->events |= NT_EV_HIT;
        return;
    }
    b->stood[p] = true;
    bj_end_turn(g);
}

float nt_bj_flip(const nt_t *g, int p, int i)
{
    int idx = p == 0 ? i : g->bj.hand_n[0] + i;
    double f = (g->t - BJ_FLIP0 - idx * BJ_FLIP_STEP) / 0.25;
    return (float)(f < 0 ? 0 : f > 1 ? 1 : f);
}

static void bj_reveal_update(nt_t *g)
{
    int n = g->bj.hand_n[0] + g->bj.hand_n[1];
    int started = (int)floor((g->t - BJ_FLIP0) / BJ_FLIP_STEP) + 1;
    started = started < 0 ? 0 : started > n ? n : started;
    if (started > g->sfx_mark) { g->sfx_mark = (uint8_t)started; g->events |= NT_EV_FLIP; }
    if (g->result.has || g->t < BJ_FLIP0 + n * BJ_FLIP_STEP + 0.4) return;
    nt_total_t a = nt_hand_total(g->bj.hand[0], g->bj.hand_n[0]);
    nt_total_t b = nt_hand_total(g->bj.hand[1], g->bj.hand_n[1]);
    int w = a.bust && b.bust ? -1 : a.bust ? 1 : b.bust ? 0 : a.total == b.total ? -1 : a.total > b.total ? 0 : 1;
    nt_win_round(g, w);
    g->result.bj[0] = a;
    g->result.bj[1] = b;
}

// ---------- menu, pause, keys ----------

static void menu_key(nt_t *g, nt_key_t k)
{
    if (k == NT_KEY_UP || k == NT_KEY_DOWN) {
        menu_select(g, (g->menu + (k == NT_KEY_DOWN ? 1 : NT_MENU_COUNT - 1)) % NT_MENU_COUNT);
        g->events |= NT_EV_MOVE;
        return;
    }
    if (k != NT_KEY_OK) return;
    g->events |= NT_EV_OK;
    if (g->menu == NT_MENU_TARGET) {
        g->cfg.target = g->cfg.target >= 7 ? 3 : g->cfg.target + 2;
        g->events |= NT_EV_SETTINGS;
        return;
    }
    start_match(g, g->menu == NT_MENU_BJ ? NT_GAME_BJ : NT_GAME_DICE);
}

bool nt_can_pause(const nt_t *g) { return !g->paused && g->scene != NT_MENU && g->scene != NT_END; }

void nt_key(nt_t *g, nt_key_t k)
{
    if (k == NT_KEY_OK_LONG) {
        if (nt_can_pause(g)) { g->paused = true; g->pause_cursor = 0; g->events |= NT_EV_OK; }
        return;
    }
    if (g->paused) {
        if (k == NT_KEY_UP || k == NT_KEY_DOWN) { g->pause_cursor ^= 1; g->events |= NT_EV_MOVE; return; }
        g->events |= NT_EV_OK;
        g->paused = false;
        if (g->pause_cursor == 1) to_menu(g);
        return;
    }
    switch (g->scene) {
    case NT_MENU: menu_key(g, k); break;
    case NT_HANDOFF:
        if (k == NT_KEY_OK) { g->events |= NT_EV_OK; begin_turn(g, g->handoff_to); }
        break;
    case NT_BJ_TURN: bj_key(g, k); break;
    case NT_DICE_TURN: nt_dice_key(g, k); break;
    case NT_BJ_REVEAL:
    case NT_DICE_REVEAL:
        if (!g->result.has) break;
        if (k == NT_KEY_OK) { g->events |= NT_EV_OK; after_result(g); }
        else { g->choice ^= 1; g->events |= NT_EV_MOVE; }
        break;
    case NT_END:
        if (k != NT_KEY_OK) { g->choice ^= 1; g->events |= NT_EV_MOVE; break; }
        g->events |= NT_EV_OK;
        if (g->choice == 0) start_match(g, g->game);
        else to_menu(g);
        break;
    default: break;
    }
}

// ---------- update ----------

static void dice_roll_update(nt_t *g)
{
    if (g->anim == NT_ANIM_SHAKE && g->t >= 1.1) {
        nt_play(g, NT_ANIM_SLAM, false);
        g->events |= NT_EV_SLAM;
    }
    if (g->t > 1.1 + fmax(0.8, nt_anim_len(g, NT_ANIM_SLAM))) nt_handoff(g, g->starter);
}

void nt_update(nt_t *g, double dt)
{
    g->clock += dt;
    g->t += dt;
    anim_update(g, dt);
    if (g->paused) return;
    switch (g->scene) {
    case NT_BJ_DEAL: bj_deal_update(g); break;
    case NT_BJ_REVEAL: bj_reveal_update(g); break;
    case NT_DICE_ROLL: dice_roll_update(g); break;
    case NT_DICE_REVEAL: nt_dice_reveal_update(g); break;
    default: break;
    }
}

uint32_t nt_take_events(nt_t *g)
{
    uint32_t e = g->events;
    g->events = 0;
    return e;
}

void nt_init(nt_t *g, const nt_config_t *cfg, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->cfg = *cfg;
    g->rng = seed;
    g->anim = NT_ANIM_IDLE;
    menu_select(g, 0);
}
