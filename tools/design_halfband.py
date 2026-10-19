#!/usr/bin/env python3
"""Designs the radio's 31-tap half-band decimator (nabo_radio_dsp.c, kSide)."""
import numpy as np
from scipy.signal import remez, freqz
N = 31
h = remez(N, [0, 0.20, 0.30, 0.5], [1, 0], fs=1.0)
c = N // 2
h[[i for i in range(N) if i != c and (i - c) % 2 == 0]] = 0
h[c] = 0.5
q = np.round(h * 32768).astype(int)
w, H = freqz(q / 32768, worN=8192, fs=1.0)
print("kSide =", [int(q[i]) for i in range(0, c, 2)])
print("stopband %.1f dB, passband ripple %.4f" % (20 * np.log10(np.abs(H[w >= 0.30]).max()), np.abs(np.abs(H[w <= 0.20]) - 1).max()))
