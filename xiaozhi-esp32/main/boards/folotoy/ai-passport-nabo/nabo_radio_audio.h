#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Output-only transition shaping. The caller counts decoded samples before
 * this step, so a synthetic starvation tail never looks like decoded music.
 * State belongs to the I2S owner and needs no queue or heap allocation. */
typedef struct {
    int16_t last;
    unsigned remaining;
    bool silent;
} nrf_audio_tail_t;

static inline void nrf_audio_smooth(nrf_audio_tail_t* tail, int16_t* samples, size_t decoded,
                                    size_t count, unsigned rate) {
    if (!tail || !samples || !count)
        return;
    if (decoded > count)
        decoded = count;
    if (decoded) {
        if (tail->silent) {
            /* A burst can refill the ring before a 30 ms tail has finished.
             * Join its current level to the resumed PCM instead of jumping
             * from that level to the decoder's zero-start fade-in. The same
             * join covers a pause tail requested just after a short block. */
            size_t blend = rate * 5 / 1000;
            if (blend > decoded)
                blend = decoded;
            for (size_t i = 0; i < blend; ++i)
                samples[i] = (int16_t)(((int32_t)tail->last * (int32_t)(blend - i - 1) +
                                        (int32_t)samples[i] * (int32_t)(i + 1)) /
                                       (int32_t)blend);
        }
        tail->remaining = 0;
        if (decoded < count) {
            size_t fade = rate * 5 / 1000;
            if (fade > decoded)
                fade = decoded;
            for (size_t i = 0; i < fade; ++i)
                samples[decoded - fade + i] = (int16_t)((int32_t)samples[decoded - fade + i] *
                                                        (int32_t)(fade - i - 1) / (int32_t)fade);
            tail->last = 0;
            tail->silent = true;
        } else {
            tail->last = samples[decoded - 1];
            tail->silent = false;
        }
        return;
    }
    if (!tail->silent) {
        tail->remaining = rate * 30 / 1000;
        tail->silent = true;
    }
    for (size_t i = 0; i < count && tail->remaining; ++i) {
        tail->last = (int16_t)((int32_t)tail->last * (int32_t)(tail->remaining - 1) /
                               (int32_t)tail->remaining);
        samples[i] = tail->last;
        --tail->remaining;
    }
}
