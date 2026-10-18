// Liar's dice for 2..4 players: rolling, the two-step bid picker (quantity, then face), legality,
// "open" and the reveal count with wild ones and zhai. Same rules as the prototype.
#include "nabo_table_model.h"

#include <math.h>
#include <string.h>

// With wild ones, a bid on ones ranks above every other face of the same quantity.
static int face_rank(int f, bool wild) { return wild && f == 1 ? 7 : f; }
static int bid_key(int q, int f, bool wild) { return q * 10 + face_rank(f, wild); }

const nt_bid_t *nt_last_bid(const nt_t *g)
{
    return g->dc.bid_n ? &g->dc.bids[g->dc.bid_n - 1] : NULL;
}

bool nt_zhai(const nt_t *g)
{
    if (!g->dc.wild) return false;
    for (int i = 0; i < g->dc.bid_n; i++)
        if (g->dc.bids[i].f == 1) return true;
    return false;
}

bool nt_wild_counts(const nt_t *g, int face) { return g->dc.wild && !nt_zhai(g) && face != 1; }

int nt_count_face(const nt_t *g, int face)
{
    bool wild = nt_wild_counts(g, face);
    int n = 0;
    for (int p = 0; p < g->np; p++)
        for (int i = 0; i < g->dc.per; i++) {
            int v = g->dc.dice[p][i];
            if (v == face || (wild && v == 1)) n++;
        }
    return n;
}

void nt_dice_new_round(nt_t *g)
{
    nt_dice_t *d = &g->dc;
    memset(d, 0, sizeof(*d));
    d->per = g->cfg.dice_per;
    d->min_open = g->cfg.min_open > g->np ? g->cfg.min_open : g->np;   // at least one die per player
    d->wild = g->cfg.wild;
    d->caller = -1;
    // Draw order matters for replay: every player's angles first, then every player's dice.
    for (int p = 0; p < g->np; p++)
        for (int i = 0; i < d->per; i++) {
            double quarter = floor(nt_rand(g) * 4) * 90;
            double sign = nt_rand(g) < 0.5 ? -1 : 1;
            d->yaw[p][i] = (float)(quarter + sign * (15 + nt_rand(g) * 16));
        }
    for (int p = 0; p < g->np; p++)
        for (int i = 0; i < d->per; i++) d->dice[p][i] = (uint8_t)(1 + (int)floor(nt_rand(g) * 6));
}

// Faces that would beat the last bid at quantity q, lowest rank first. Returns the count.
static int legal_faces(const nt_t *g, int q, uint8_t out[6])
{
    const nt_dice_t *d = &g->dc;
    const nt_bid_t *last = nt_last_bid(g);
    int lk = last ? bid_key(last->q, last->f, d->wild) : -1, n = 0;
    if (q < d->min_open) return 0;
    for (int rank = 1; rank <= 7; rank++) {
        int f = rank == 7 ? 1 : rank;
        if (face_rank(f, d->wild) != rank) continue;
        if (bid_key(q, f, d->wild) > lk) out[n++] = (uint8_t)f;
    }
    return n;
}

void nt_dice_build_bids(nt_t *g)
{
    nt_dice_t *d = &g->dc;
    const nt_bid_t *last = nt_last_bid(g);
    uint8_t faces[6];
    d->qn = 0;
    if (last) d->qlist[d->qn++] = 0;   // "open"
    for (int q = d->min_open; q <= d->per * g->np && d->qn < NT_MAX_QTY; q++)
        if (legal_faces(g, q, faces)) d->qlist[d->qn++] = (uint8_t)q;
    d->step = NT_STEP_QTY;
    d->qi = last && d->qn > 1 ? 1 : 0;   // start on the smallest raise, never on "open"
}

static void enter_face_step(nt_t *g)
{
    nt_dice_t *d = &g->dc;
    const nt_bid_t *last = nt_last_bid(g);
    uint8_t faces[6];
    int n = legal_faces(g, d->qlist[d->qi], faces);
    d->flist[0] = 0;   // "back"
    memcpy(d->flist + 1, faces, n);
    d->fn = (uint8_t)(n + 1);
    d->step = NT_STEP_FACE;
    d->fi = 1;   // keep the last bid's face when it is still legal, else the lowest legal face
    for (int i = 1; last && i < d->fn; i++)
        if (d->flist[i] == last->f) d->fi = (uint8_t)i;
}

void nt_dice_key(nt_t *g, nt_key_t k)
{
    nt_dice_t *d = &g->dc;
    if (k == NT_KEY_UP || k == NT_KEY_DOWN) {
        int dir = k == NT_KEY_UP ? 1 : -1;
        uint8_t *at = d->step == NT_STEP_QTY ? &d->qi : &d->fi;
        int len = d->step == NT_STEP_QTY ? d->qn : d->fn;
        int n = *at + dir;
        n = n < 0 ? 0 : n > len - 1 ? len - 1 : n;
        if (n != *at) { *at = (uint8_t)n; g->events |= NT_EV_MOVE; }
        return;
    }
    if (k != NT_KEY_OK) return;
    if (d->step == NT_STEP_QTY) {
        if (d->qlist[d->qi] != 0) { enter_face_step(g); g->events |= NT_EV_OK; return; }
        d->caller = (int8_t)g->turn;
        nt_go(g, NT_DICE_REVEAL);
        nt_play(g, NT_ANIM_OPEN, false);
        g->events |= NT_EV_OPEN;
        return;
    }
    int f = d->flist[d->fi];
    if (f == 0) { d->step = NT_STEP_QTY; g->events |= NT_EV_OK; return; }
    if (d->bid_n < NT_MAX_BIDS) {
        nt_bid_t b = {g->turn, d->qlist[d->qi], (uint8_t)f};
        d->bids[d->bid_n++] = b;
    }
    g->events |= NT_EV_OK;
    nt_handoff(g, nt_next_player(g, g->turn));
}

void nt_dice_reveal_update(nt_t *g)
{
    if (!g->sfx_mark && g->t >= 0.4) { g->sfx_mark = 1; g->events |= NT_EV_LIFT; }
    if (g->result.has || g->t < 1.3) return;
    const nt_bid_t *last = nt_last_bid(g);
    int actual = nt_count_face(g, last->f);
    bool held = actual >= last->q;
    int bidder = last->p, caller = g->dc.caller;   // whoever was wrong takes the penalty
    nt_finish_round(g, 1u << (held ? bidder : caller), held ? caller : bidder);
    g->result.actual = (uint8_t)actual;
    g->result.bid_q = last->q;
    g->result.bid_f = last->f;
    g->result.bid_held = held;
}
