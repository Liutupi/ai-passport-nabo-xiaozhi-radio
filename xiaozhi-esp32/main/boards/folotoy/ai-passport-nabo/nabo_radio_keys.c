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
            if (i == NRF_DOWN) {
                k->repeat_at = now + 220;
                k->repeats = 0;
            }
        }
    }
    if (k->down[NRF_DOWN] && k->held[NRF_DOWN] && (int32_t)(now - k->repeat_at) >= 0) {
        k->emit(k->ctx, NRF_DOWN, NRF_REPEAT);
        if (k->repeats < 12)
            ++k->repeats;
        k->repeat_at = now + 220 - k->repeats * 13;
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
