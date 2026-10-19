"""Host tests for the production radio output stage (nabo_radio_dsp.c)."""
import ctypes, math, random, subprocess, tempfile
from pathlib import Path
from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
I16 = ctypes.c_int16

with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
    lib_path = Path(folder) / 'dsp.dll'
    subprocess.run([CC, '-shared', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    str(BOARD / 'nabo_radio_dsp.c'), '-o', str(lib_path)], check=True)
    lib = ctypes.CDLL(str(lib_path))
    lib.nabo_radio_dsp_feed.restype = ctypes.c_size_t
    lib.nabo_radio_dsp_silent.restype = ctypes.c_bool
    lib.nabo_radio_dsp_meter.restype = ctypes.c_int

    def make(rate=48000, loud=False, fade_in=True):
        d = ctypes.create_string_buffer(512)
        lib.nabo_radio_dsp_init(d, rate)
        lib.nabo_radio_dsp_set_loudness(d, loud)
        if fade_in:
            lib.nabo_radio_dsp_fade(d, True, 1)
        return d

    def run(d, samples, channels=1, chunks=None):
        out = []
        frames = len(samples) // channels
        chunks = chunks or [frames]
        at = 0
        for n in chunks:
            n = min(n, frames - at)
            if n <= 0:
                break
            src = (I16 * (n * channels))(*samples[at * channels:(at + n) * channels])
            dst = (I16 * (n + 2))()
            got = lib.nabo_radio_dsp_feed(d, src, n, channels, dst, n + 2)
            out += dst[:got]
            at += n
        return out

    def sine(freq, rate, seconds, amp):
        return [int(amp * math.sin(2 * math.pi * freq * i / rate)) for i in range(int(rate * seconds))]

    def rms(x):
        return math.sqrt(sum(v * v for v in x) / len(x))

    checks = 0
    # 1. passband: 1 kHz and 8 kHz through 48 k -> 24 k keep their level (minus 2 dB headroom)
    for f in (1000, 8000):
        out = run(make(), sine(f, 48000, 0.5, 8000))[2000:]
        gain = rms(out) / (8000 / math.sqrt(2) * 0.794)
        assert 0.9 < gain < 1.06, (f, gain)
        checks += 1
    # 2. alias rejection: 20 kHz would fold to 4 kHz; it must be at least 50 dB down
    out = run(make(), sine(20000, 48000, 0.5, 8000))[2000:]
    assert 20 * math.log10(rms(out) / (8000 / math.sqrt(2)) + 1e-9) < -50, rms(out)
    checks += 1
    # 3. chunk sizes do not change a single sample
    random.seed(7)
    noise = [random.randint(-12000, 12000) for _ in range(48000)]
    whole = run(make(loud=True), noise, 2)
    pieces = []
    rest = 24000
    while rest > 0:
        n = random.choice([1, 2, 3, 7, 160, 576, 1152])
        pieces.append(min(n, rest))
        rest -= n
    assert run(make(loud=True), noise, 2, pieces) == whole
    checks += 1
    # 4. stereo downmix: opposite channels cancel
    s = sine(1000, 48000, 0.2, 9000)
    inter = [v for x in s for v in (x, -x)]
    assert max(abs(v) for v in run(make(), inter, 2)) <= 1
    checks += 1
    # 5. limiter: full-scale square with maximum boost never wraps or reaches full scale
    sq = [32767 if (i // 24) % 2 else -32768 for i in range(48000)]
    out = run(make(loud=True), sq)
    assert max(abs(v) for v in out) < 32767 and all(-32768 < v < 32767 for v in out)
    checks += 1
    # 6. fade-out: 60 ms ramp is monotonic and ends silent
    d = make()
    run(d, sine(1000, 48000, 0.2, 10000))
    lib.nabo_radio_dsp_fade(d, False, 60)
    out = run(d, sine(1000, 48000, 0.1, 10000))
    blocks = [max(abs(v) for v in out[i:i + 24]) for i in range(0, 1440, 24)]
    assert all(b2 <= b1 + 2 for b1, b2 in zip(blocks, blocks[1:])), blocks
    assert max(abs(v) for v in out[1460:]) == 0 and lib.nabo_radio_dsp_silent(d)
    checks += 1
    # 7. leveller: a quiet station is raised, a loud one lowered, silence stays silent
    def settled(amp):
        d = make(loud=True)
        out = run(d, sine(1000, 48000, 8.0, amp))
        return rms(out[-24000:])
    quiet, loud = settled(1500), settled(16000)
    plain_quiet = 1500 / math.sqrt(2) * 0.794
    assert quiet > plain_quiet * 2.0, (quiet, plain_quiet)          # raised at least 6 dB
    assert loud < 16000 / math.sqrt(2) * 0.794 * 0.75, loud         # lowered at least 2.5 dB
    assert loud / quiet < 3.0, (loud, quiet)                         # 21 dB spread -> under 10 dB
    assert quiet > plain_quiet * 4.0, ('quiet station still too soft', quiet)  # old target gave ~2x
    hiss = run(make(loud=True), [random.randint(-40, 40) for _ in range(48000 * 4)])
    assert rms(hiss[-12000:]) < 40, rms(hiss[-12000:])
    checks += 1
    # 8. high-pass: 40 Hz is at least 6 dB below 1 kHz
    low = rms(run(make(), sine(40, 48000, 1.0, 8000))[12000:])
    mid = rms(run(make(), sine(1000, 48000, 1.0, 8000))[12000:])
    assert 20 * math.log10(low / mid) < -6, (low, mid)
    checks += 1
    # 9. 44.1 kHz and 32 kHz streams decimate the same way (output 22.05 k / 16 k)
    for rate in (44100, 32000):
        out = run(make(rate), sine(1000, rate, 0.5, 8000))[2000:]
        assert 0.9 < rms(out) / (8000 / math.sqrt(2) * 0.794) < 1.06
        checks += 1
    # 10. 22.05 kHz streams pass through at their own rate
    out = run(make(22050), sine(1000, 22050, 0.5, 8000))
    assert len(out) == 11025 and 0.9 < rms(out[2000:]) / (8000 / math.sqrt(2) * 0.794) < 1.06
    lib.nabo_radio_dsp_out_rate_for.restype = ctypes.c_uint
    assert [lib.nabo_radio_dsp_out_rate_for(r) for r in (48000, 44100, 32000, 24000, 22050, 16000)] == [24000, 22050, 16000, 24000, 22050, 16000]
    checks += 2
    # 11. meter follows the output and decays
    d = make()
    run(d, sine(1000, 48000, 0.1, 20000))
    m1 = lib.nabo_radio_dsp_meter(d)
    for _ in range(30):
        m2 = lib.nabo_radio_dsp_meter(d)
    assert m1 > 100 and m2 < m1 // 4, (m1, m2)
    checks += 1
    # Full-scale polarity reversals must produce a transient of the correct
    # sign. Q16 differences exceed int32 here; wrapping reverses the pulse.
    for rate in (8000, 16000, 22050, 24000):
        d = make(rate)
        run(d, [-32768] * 2048)
        edge = run(d, [32767] * 2048)
        assert edge[0] > 20000 and all(x >= 0 for x in edge), (rate, edge[:8])
        edge = run(d, [-32768] * 2048)
        assert edge[0] < -20000 and all(x <= 0 for x in edge), (rate, edge[:8])
        assert abs(edge[-1]) <= 1
        checks += 1
    unload_library(lib._handle)
print(f'PASS: {checks} radio output-stage checks (half-band 2:1, leveller, fade, limiter, high-pass, meter)')
