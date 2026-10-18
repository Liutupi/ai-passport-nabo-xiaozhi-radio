// Who is the undercover, for 3..8 players passing the badge: setup, dealing the words one
// player at a time behind hand-off screens, describing rounds, voting someone out on the badge,
// and the reveal. Same rules and the same random draws as the browser prototype.
#include <string.h>

#include "nabo_table_model.h"
#include "nabo_uc_words.h"

const char *nt_uc_word(const nt_t *g, int p)
{
    const nt_uc_t *u = &g->uc;
    return NT_UC_WORDS[u->pair][u->uc[p] == u->swap ? 1 : 0];
}

int nt_uc_alive(const nt_t *g, bool undercover)
{
    int n = 0;
    for (int p = 0; p < g->uc.n; p++) n += g->uc.alive[p] && g->uc.uc[p] == undercover;
    return n;
}

int nt_uc_list_len(const nt_t *g) { return nt_uc_alive(g, false) + nt_uc_alive(g, true) + 1; }

int nt_uc_list_player(const nt_t *g, int row)
{
    for (int p = 0; p < g->uc.n; p++)
        if (g->uc.alive[p] && row-- == 0) return p;
    return -1;
}

static void enter_scene(nt_t *g, nt_scene_t scene, nt_anim_t anim)
{
    nt_go(g, scene);
    nt_play(g, anim, false);
}

void nt_uc_setup(nt_t *g)
{
    g->game = NT_GAME_UC;
    g->menu = NT_MENU_UC;   // opened straight from voice: the cover returns to this item too
    g->uc.cursor = g->cfg.uc_players;
    enter_scene(g, NT_UC_SETUP, NT_ANIM_IDLE);
}

static bool recently_dealt(const nt_uc_t *u, int pair)
{
    for (int i = 0; i < u->recent_n; i++)
        if (u->recent[i] == pair) return true;
    return false;
}

// Deal a new game: a pair not dealt recently, which word the undercovers get, and their seats.
static void deal(nt_t *g)
{
    nt_uc_t *u = &g->uc;
    int pair = nt_rand_int(g, NT_UC_PAIRS);
    for (int tries = 0; tries < 64 && recently_dealt(u, pair); tries++) pair = nt_rand_int(g, NT_UC_PAIRS);
    if (u->recent_n == NT_UC_RECENT) memmove(u->recent, u->recent + 1, sizeof(u->recent[0]) * --u->recent_n);
    u->recent[u->recent_n++] = (uint16_t)pair;
    u->pair = (uint16_t)pair;
    u->swap = nt_rand(g) < 0.5;
    u->n_uc = u->n >= 7 ? 2 : 1;
    memset(u->uc, 0, sizeof(u->uc));
    for (int k = 0; k < u->n_uc; k++) {   // the j-th seat that is not an undercover yet
        int j = nt_rand_int(g, u->n - k);
        for (int p = 0; p < u->n; p++)
            if (!u->uc[p] && j-- == 0) { u->uc[p] = true; break; }
    }
    for (int p = 0; p < u->n; p++) u->alive[p] = p < u->n;
    u->round = 1;
    u->out = 0xff;
    u->speaker = 0xff;
    u->recheck = false;
    u->confirm = false;
    g->round++;
    nt_handoff(g, 0);
}

// The describing screen; a new round picks who starts.
static void to_talk(nt_t *g, bool new_round)
{
    nt_uc_t *u = &g->uc;
    if (new_round) {
        if (u->speaker == 0xff) {   // first round: anyone may start
            int j = nt_rand_int(g, nt_uc_alive(g, false) + nt_uc_alive(g, true));
            u->speaker = (uint8_t)nt_uc_list_player(g, j);
        } else if (u->out != 0xff && !u->alive[u->speaker]) {   // the starter was voted out
            int p = u->speaker;
            do p = (p + 1) % u->n; while (!u->alive[p]);
            u->speaker = (uint8_t)p;
        }
    }
    u->cursor = 0;
    u->confirm = false;
    enter_scene(g, NT_UC_TALK, NT_ANIM_IDLE);
}

void nt_uc_begin_turn(nt_t *g, int p)
{
    g->turn = (uint8_t)p;
    enter_scene(g, NT_UC_WORD, NT_ANIM_IDLE);
}

static void vote_out(nt_t *g, int p)
{
    nt_uc_t *u = &g->uc;
    u->alive[p] = false;
    u->out = (uint8_t)p;
    int civ = nt_uc_alive(g, false), und = nt_uc_alive(g, true);
    g->result.has = civ == 0 || und == 0 || und >= civ;
    g->result.winners = 0;   // card-game fields stay neutral; the outcome is uc_won
    g->result.loser = -1;
    u->uc_won = und > 0;
    // NABO is surprised by a caught undercover and points at a civilian voted out by mistake.
    enter_scene(g, NT_UC_OUT, u->uc[p] ? NT_ANIM_SURPRISE : NT_ANIM_POINT);
    g->mirror = !u->uc[p] && (p & 1);
    g->events |= u->uc[p] ? NT_EV_OPEN : NT_EV_FLIP;
}

static int step(int at, int len, nt_key_t k)
{
    int n = at + (k == NT_KEY_DOWN ? 1 : -1);
    return n < 0 ? 0 : n > len - 1 ? len - 1 : n;
}

void nt_uc_key(nt_t *g, nt_key_t k)
{
    nt_uc_t *u = &g->uc;
    bool move = k == NT_KEY_UP || k == NT_KEY_DOWN;
    if (k != NT_KEY_OK && !move) return;
    g->events |= move ? NT_EV_MOVE : NT_EV_OK;
    switch (g->scene) {
    case NT_UC_SETUP:
        if (k == NT_KEY_UP && u->cursor < NT_UC_MAX) u->cursor++;
        else if (k == NT_KEY_DOWN && u->cursor > NT_UC_MIN) u->cursor--;
        else if (k == NT_KEY_OK) {
            if (g->cfg.uc_players != u->cursor) { g->cfg.uc_players = u->cursor; g->events |= NT_EV_SETTINGS; }
            u->n = u->cursor;
            u->recent_n = 0;
            g->round = 0;
            deal(g);
            g->events |= NT_EV_SHUFFLE;
        }
        break;
    case NT_UC_WORD:
        if (k != NT_KEY_OK) break;
        if (u->recheck) { u->recheck = false; to_talk(g, false); }
        else if (g->turn + 1 < u->n) nt_handoff(g, g->turn + 1);
        else to_talk(g, true);
        break;
    case NT_UC_TALK:
        if (move) { u->cursor ^= 1; break; }
        u->list = u->cursor ? NT_UC_LIST_FORGOT : NT_UC_LIST_VOTE;
        u->cursor = 0;
        u->confirm = false;
        enter_scene(g, NT_UC_VOTE, NT_ANIM_IDLE);
        break;
    case NT_UC_VOTE: {
        if (u->confirm) {
            if (move) { u->confirm_cursor ^= 1; break; }
            u->confirm = false;
            if (u->confirm_cursor == 0) vote_out(g, nt_uc_list_player(g, u->cursor));
            break;
        }
        if (move) { u->cursor = (uint8_t)step(u->cursor, nt_uc_list_len(g), k); break; }
        int p = nt_uc_list_player(g, u->cursor);
        if (u->list == NT_UC_LIST_FORGOT) {
            if (p < 0) { to_talk(g, false); break; }   // "back"
            u->recheck = true;
            nt_handoff(g, p);
        } else if (p < 0) {   // a tied vote: describe once more
            u->round++;
            to_talk(g, false);
        } else {
            u->confirm = true;
            u->confirm_cursor = 0;
        }
        break;
    }
    case NT_UC_OUT:
        if (k != NT_KEY_OK) break;
        if (g->result.has) {
            g->choice = 0;
            enter_scene(g, NT_UC_END, NT_ANIM_TROPHY);
            g->events |= NT_EV_WIN;
        } else {
            u->round++;
            to_talk(g, true);
        }
        break;
    case NT_UC_END:
        if (move) { g->choice ^= 1; break; }
        if (g->choice == 1) { nt_to_menu(g); break; }
        memset(&g->result, 0, sizeof(g->result));
        deal(g);
        g->events |= NT_EV_SHUFFLE;
        break;
    default:
        break;
    }
}
