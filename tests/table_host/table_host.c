// Host shim for test_table.py: drives the NABO TABLE model and prints state snapshots in
// the same JSON shape that record_golden.js records from the browser prototype.
#include <stdio.h>
#include <string.h>

#include "nabo_table_model.h"

static nt_t g;

nt_t *host_model(void) { return &g; }

void host_init(unsigned seed, int players, int target, int per, int min_open, int wild, float cut_s, float slam_s)
{
    nt_config_t cfg = {0};
    cfg.players = (uint8_t)players;
    cfg.target = (uint8_t)target;
    cfg.dice_per = (uint8_t)per;
    cfg.min_open = (uint8_t)min_open;
    cfg.wild = wild != 0;
    cfg.anim_s[NT_ANIM_SHUFFLE] = cut_s;   // the cut at the start of a round
    cfg.anim_s[NT_ANIM_SLAM] = slam_s;
    nt_init(&g, &cfg, seed);
}

void host_key(int k) { nt_key(&g, (nt_key_t)k); }

void host_frames(int n)
{
    for (int i = 0; i < n; i++) nt_update(&g, 1.0 / 60);
}

unsigned host_events(void) { return nt_take_events(&g); }

#define PUT(...) (len += snprintf(out + len, len < cap ? cap - len : 0, __VA_ARGS__))

static int ints(char *out, int len, int cap, const uint8_t *v, int n)
{
    PUT("[");
    for (int i = 0; i < n; i++) PUT("%s%d", i ? "," : "", v[i]);
    PUT("]");
    return len;
}

int host_snap(char *out, int cap)
{
    int len = 0;
    int w = !g.result.has ? -2 : g.result.winners, lo = !g.result.has ? -2 : g.result.loser;
    PUT("{\"sc\":%d,\"g\":%d,\"np\":%d,\"score\":", g.scene, g.game, g.np);
    len = ints(out, len, cap, g.score, g.np);
    PUT(",\"round\":%d,\"starter\":%d,\"turn\":%d,\"ho\":%d,\"pause\":%d,"
        "\"choice\":%d,\"menu\":%d,\"target\":%d,\"players\":%d,\"w\":%d,\"lo\":%d",
        g.round, g.starter, g.turn, g.handoff_to, g.paused ? g.pause_cursor : -1, g.choice, g.menu, g.cfg.target,
        g.cfg.players, w, lo);
    if (g.game == NT_GAME_BJ) {
        PUT(",\"bj\":{\"h\":[");
        for (int p = 0; p < g.np; p++) {
            PUT("%s[", p ? "," : "");
            for (int i = 0; i < g.bj.hand_n[p]; i++) PUT("%s%d", i ? "," : "", g.bj.hand[p][i].r * 10 + g.bj.hand[p][i].s);
            PUT("]");
        }
        PUT("],\"st\":[");
        for (int p = 0; p < g.np; p++) PUT("%s%d", p ? "," : "", g.bj.stood[p]);
        PUT("],\"ph\":%d,\"cu\":%d,\"d\":%d,\"n\":%d}", g.bj.phase, g.bj.cursor, g.bj.dealt, g.bj.deck_n);
    }
    if (g.game == NT_GAME_DICE) {
        const nt_dice_t *d = &g.dc;
        PUT(",\"dc\":{\"d\":[");
        for (int p = 0; p < g.np; p++) {
            if (p) PUT(",");
            len = ints(out, len, cap, d->dice[p], d->per);
        }
        PUT("],\"y\":[");
        for (int p = 0; p < g.np; p++) {
            PUT("%s[", p ? "," : "");
            for (int i = 0; i < d->per; i++) PUT("%s%.0f", i ? "," : "", (double)d->yaw[p][i] * 1000);
            PUT("]");
        }
        PUT("],\"b\":[");
        for (int i = 0; i < d->bid_n; i++) PUT("%s[%d,%d,%d]", i ? "," : "", d->bids[i].p, d->bids[i].q, d->bids[i].f);
        PUT("],\"st\":%d,\"q\":", d->step);
        len = ints(out, len, cap, d->qlist, d->qn);
        PUT(",\"qi\":%d,\"f\":", d->qi);
        len = ints(out, len, cap, d->flist, d->fn);
        PUT(",\"fi\":%d,\"ca\":%d}", d->fi, d->caller);
    }
    PUT("}");
    return len;
}

// Direct setup for rule tests that a random replay may never reach.
void host_set_dice(int p, int i, int v) { g.dc.dice[p][i] = (uint8_t)v; }

void host_add_bid(int p, int q, int f)
{
    nt_bid_t b = {(uint8_t)p, (uint8_t)q, (uint8_t)f};
    g.dc.bids[g.dc.bid_n++] = b;
}

int host_count(int face) { return nt_count_face(&g, face); }

// 21 points: give player p the ranks in order (suits all spades), then reveal and score.
void host_set_hand(int p, const int *ranks, int n)
{
    for (int i = 0; i < n; i++) g.bj.hand[p][i] = (nt_card_t){(uint8_t)ranks[i], 0, -10};
    g.bj.hand_n[p] = (uint8_t)n;
    g.bj.stood[p] = true;
}

void host_bj_reveal(void)
{
    memset(&g.result, 0, sizeof(g.result));
    nt_go(&g, NT_BJ_REVEAL);
    for (int i = 0; i < 600 && !g.result.has; i++) nt_update(&g, 1.0 / 60);
}

void host_set_score(int p, int v) { g.score[p] = (uint8_t)v; }
int host_match_over(void) { return nt_match_over(&g); }
