#pragma once
/* NABO FM output stage: decoded radio PCM -> speaker PCM at half the stream rate.
 *
 * mono downmix -> 31-tap half-band FIR, 2:1 decimation (48 k -> 24 k, 44.1 k -> 22.05 k,
 * 32 k -> 16 k; streams at 24 k or below pass through undecimated) -> 120 Hz high-pass -> slow loudness leveller -> fade -> headroom ->
 * soft limiter. Integer only; no allocation; chunk sizes do not change the result.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NABO_RADIO_DSP_TAPS 31

typedef struct {
    int16_t hist[2 * NABO_RADIO_DSP_TAPS]; /* doubled ring: a window is always contiguous */
    uint8_t pos, odd, decimate;
    int64_t hp_lp;       /* Q16; FIR overshoot and full-scale edges need >32 bits */
    int32_t env;         /* loudness envelope (mean |x|), Q12 */
    int32_t agc;         /* loudness gain, Q20 (1 << 20 = 0 dB) */
    int32_t agc_target;  /* control-rate target; sample-rate smoothing stays below */
    uint8_t gain_phase;
    int32_t fade;        /* fade gain, Q15 */
    int32_t fade_target; /* Q15 */
    int32_t fade_step;   /* per output sample, Q15 */
    int32_t peak;        /* meter, 0..32767 */
    unsigned out_rate;
    bool loudness;
} nabo_radio_dsp_t;

/* stream_rate is the decoded rate; the speaker runs at nabo_radio_dsp_out_rate(). */
void nabo_radio_dsp_init(nabo_radio_dsp_t* d, unsigned stream_rate);
/* New station: clear filter history but keep the leveller's gain so volume does not jump. */
void nabo_radio_dsp_reset(nabo_radio_dsp_t* d, unsigned stream_rate);
/* Speaker rate for a stream rate: half above 24 kHz, otherwise the same. */
unsigned nabo_radio_dsp_out_rate_for(unsigned stream_rate);
/* Converts frames of interleaved PCM (1 or 2 channels). Writes at most cap samples and
 * returns how many; cap >= frames + 1 always suffices. */
size_t nabo_radio_dsp_feed(nabo_radio_dsp_t* d, const int16_t* pcm, size_t frames, int channels,
                           int16_t* out, size_t cap);
/* Ramp to full level (in) or silence over ms milliseconds. */
void nabo_radio_dsp_fade(nabo_radio_dsp_t* d, bool in, unsigned ms);
/* True once a fade-out has reached silence. */
bool nabo_radio_dsp_silent(const nabo_radio_dsp_t* d);
void nabo_radio_dsp_set_loudness(nabo_radio_dsp_t* d, bool on);
/* Decaying output peak for the console meter, 0..255. Call once per UI frame. */
int nabo_radio_dsp_meter(nabo_radio_dsp_t* d);

#ifdef __cplusplus
}
#endif
