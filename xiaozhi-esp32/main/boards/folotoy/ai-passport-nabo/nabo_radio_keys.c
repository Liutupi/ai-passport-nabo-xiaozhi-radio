#include "nabo_radio_keys.h"
#include <string.h>
void nrf_keys_init(nrf_keys_t* k, nrf_key_fn fn, void* ctx) {
    memset(k, 0, sizeof(*k));
    k->emit = fn;
    k->ctx = ctx;
}
void nrf_keys_tick(nrf_keys_t* k, uint32_t now) {
    for (int i = 0; i < 3; i++) {
        if (k->pending[i] && (int32_t)(now - k->pending[i]) >= 0) {
            k->pending[i] = 0;
            if (i == NRF_OK)
                k->emit(k->ctx, i, NRF_CLICK);
        }
        if (k->down[i] && !k->held[i] && now - k->since[i] >= 650) {
            k->held[i] = true;
            k->pending[i] = 0;
            k->emit(k->ctx, i, NRF_LONG);
            if (i != NRF_OK) {
                k->repeat_at[i] = now + 220;
                k->repeats[i] = 0;
            }
        }
        if (i != NRF_OK && k->down[i] && k->held[i] && (int32_t)(now - k->repeat_at[i]) >= 0) {
            k->emit(k->ctx, i, NRF_REPEAT);
            // DOWN retains accelerated list scrolling. UP repeats at a steady
            // rate; the scene limits both volume directions to the same pace.
            if (i == NRF_DOWN && k->repeats[i] < 12)
                ++k->repeats[i];
            k->repeat_at[i] = now + 220 - k->repeats[i] * 13;
        }
    }
}
void nrf_keys_edge(nrf_keys_t* k, int i, bool down, uint32_t now) {
    if (!k || i < 0 || i > 2 || k->down[i] == down)
        return;
    nrf_keys_tick(k, now);
    k->down[i] = down;
    if (down) {
        k->since[i] = now;
        k->held[i] = false;
        return;
    }
    if (k->held[i])
        return;
    if (i == NRF_DOWN) {
        k->emit(k->ctx, i, NRF_CLICK);
        return;
    }
    if (k->pending[i]) {
        k->pending[i] = 0;
        k->emit(k->ctx, i, NRF_DOUBLE);
    } else {
        k->pending[i] = now + 250;
        if (i == NRF_UP)
            k->emit(k->ctx, i, NRF_CLICK);
    }
}
