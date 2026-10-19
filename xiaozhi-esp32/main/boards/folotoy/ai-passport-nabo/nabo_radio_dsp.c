#include "nabo_radio_dsp.h"
#include <string.h>

/* Half-band low-pass, 31 taps, Q15; pass to 0.20 fs, stop from 0.30 fs (-57 dB).
 * Only the odd offsets from the centre are non-zero, so one output costs 8 multiplies.
 * Designed with scipy.signal.remez; see tools/design_halfband.py. */
static const int16_t kSide[8] = {-69, 152, -309, 564, -976, 1688, -3225, 10344};

#define CENTRE (NABO_RADIO_DSP_TAPS / 2)
#define LOUD_TARGET (2600 << 12) /* mean |x| the leveller aims for, Q12 (about -18 dBFS RMS) */
#define LOUD_FLOOR (180 << 12)   /* below this the input is treated as silence: no boost */
#define AGC_MIN (2048 << 8)      /* -6 dB, Q20 */
#define AGC_MAX (16384 << 8)     /* +12 dB, Q20 */
#define HEADROOM 26029           /* -2 dB, Q15 */
#define KNEE 22000               /* soft limiter starts here */

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

unsigned nabo_radio_dsp_out_rate_for(unsigned stream_rate) {
    return stream_rate > 24000 ? stream_rate / 2 : stream_rate;
}

void nabo_radio_dsp_init(nabo_radio_dsp_t* d, unsigned stream_rate) {
    memset(d, 0, sizeof(*d));
    d->agc = 4096 << 8;
    d->env = LOUD_FLOOR;
    d->loudness = true;
    stream_rate = stream_rate ? stream_rate : 48000;
    d->out_rate = nabo_radio_dsp_out_rate_for(stream_rate);
    d->decimate = stream_rate > 24000;
}

void nabo_radio_dsp_reset(nabo_radio_dsp_t* d, unsigned stream_rate) {
    memset(d->hist, 0, sizeof(d->hist));
    d->pos = d->odd = 0;
    d->hp_lp = 0;
    d->peak = 0;
    if (stream_rate) {
        d->out_rate = nabo_radio_dsp_out_rate_for(stream_rate);
        d->decimate = stream_rate > 24000;
    }
}

void nabo_radio_dsp_set_loudness(nabo_radio_dsp_t* d, bool on) {
    d->loudness = on;
    if (!on)
        d->agc = 4096 << 8;
}

void nabo_radio_dsp_fade(nabo_radio_dsp_t* d, bool in, unsigned ms) {
    d->fade_target = in ? 32767 : 0;
    uint32_t samples = (uint32_t)d->out_rate * (ms ? ms : 1) / 1000;
    int32_t span = d->fade_target - d->fade;
    if (span < 0)
        span = -span;
    d->fade_step = samples ? (int32_t)(span / samples) + 1 : 32767;
}

bool nabo_radio_dsp_silent(const nabo_radio_dsp_t* d) {
    return d->fade_target == 0 && d->fade == 0;
}

int nabo_radio_dsp_meter(nabo_radio_dsp_t* d) {
    int v = (int)(d->peak >> 7);
    d->peak -= d->peak >> 3;  /* about 20 dB/s at 30 UI frames per second */
    return v > 255 ? 255 : v;
}

static int16_t finish(nabo_radio_dsp_t* d, int32_t x) {
    /* 120 Hz-ish one-pole high-pass keeps the small speaker from slapping. */
    int32_t xs = x << 16;
    d->hp_lp += (xs - d->hp_lp) >> 5;
    x = (xs - d->hp_lp) >> 16;

    if (d->loudness) {
        int32_t mag = (x < 0 ? -x : x) << 12;
        /* Rises in about 0.34 s, falls in about 1.4 s (24 kHz). */
        d->env += mag > d->env ? (mag - d->env) >> 13 : (mag - d->env) >> 15;
        /* Pauses, talk gaps and hiss sit under the floor: drift back to unity, never boost them. */
        int32_t want = d->env > LOUD_FLOOR
                           ? clamp32((int32_t)(((int64_t)LOUD_TARGET << 20) / d->env), AGC_MIN, AGC_MAX)
                           : (1 << 20);
        d->agc += (want - d->agc) >> 12;  /* gain glides; never pumps on single beats */
        x = (int32_t)(((int64_t)x * d->agc) >> 20);
    }

    if (d->fade != d->fade_target) {
        if (d->fade < d->fade_target)
            d->fade = d->fade + d->fade_step > d->fade_target ? d->fade_target : d->fade + d->fade_step;
        else
            d->fade = d->fade - d->fade_step < d->fade_target ? d->fade_target : d->fade - d->fade_step;
    }
    x = (int32_t)(((int64_t)x * d->fade) >> 15);
    x = (int32_t)(((int64_t)x * HEADROOM) >> 15);

    /* Soft knee: above KNEE the curve bends toward, and never reaches, full scale. */
    int32_t a = x < 0 ? -x : x;
    if (a > KNEE) {
        int32_t over = a - KNEE, room = 32767 - KNEE;
        a = KNEE + (int32_t)(((int64_t)over * room) / (over + room));
        x = x < 0 ? -a : a;
    }
    int32_t m = x < 0 ? -x : x;
    if (m > d->peak)
        d->peak = m;
    return (int16_t)x;
}

size_t nabo_radio_dsp_feed(nabo_radio_dsp_t* d, const int16_t* pcm, size_t frames, int channels,
                           int16_t* out, size_t cap) {
    size_t written = 0;
    if (!d || !pcm || !out || (channels != 1 && channels != 2))
        return 0;
    for (size_t i = 0; i < frames; ++i) {
        int32_t x = channels == 2 ? ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) >> 1 : pcm[i];
        if (!d->decimate) {
            if (written >= cap)
                break;
            out[written++] = finish(d, x);
            continue;
        }
        d->hist[d->pos] = d->hist[d->pos + NABO_RADIO_DSP_TAPS] = (int16_t)x;
        d->pos = (uint8_t)(d->pos + 1 == NABO_RADIO_DSP_TAPS ? 0 : d->pos + 1);
        d->odd ^= 1;
        if (d->odd)
            continue;
        if (written >= cap)
            break;
        /* w[0] is the oldest sample of the 31-sample window, w[30] the newest. */
        const int16_t* w = d->hist + d->pos;
        int32_t acc = (int32_t)w[CENTRE] << 14;  /* centre tap 0.5 */
        for (int k = 0; k < 8; ++k)
            acc += (int32_t)kSide[k] * ((int32_t)w[2 * k] + w[NABO_RADIO_DSP_TAPS - 1 - 2 * k]);
        out[written++] = finish(d, (acc + (1 << 14)) >> 15);
    }
    return written;
}
