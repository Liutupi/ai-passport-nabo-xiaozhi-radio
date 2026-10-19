"""HTTP UTC -> monotonic Asia/Shanghai radio clock; no device/network access."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
    library = Path(folder) / 'clock.dll'
    subprocess.run([CC, '-shared', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    SHARED_EXPORT, '-DNRF_CLOCK_HOST', str(BOARD/'nabo_radio_clock.c'),
                    '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.nrf_clock_http_date.argtypes = [C.c_char_p, C.c_int64]
    lib.nrf_clock_http_date.restype = C.c_bool
    lib.nrf_clock_text.argtypes = [C.c_void_p, C.c_size_t, C.c_int64]
    lib.nrf_clock_text.restype = C.c_bool
    def text(now):
        out = C.create_string_buffer(8)
        ok = lib.nrf_clock_text(out, len(out), now)
        return ok, out.value.decode()
    assert text(0) == (False, '--:--')
    anchor = (1 << 32) * 1000 - 500000  # across the 32-bit millisecond wrap
    assert lib.nrf_clock_http_date(b'Mon, 19 Oct 2026 10:25:58 GMT', anchor)
    assert text(anchor) == (True, '18:25')
    assert text(anchor + 3000000) == (True, '18:26')
    assert text(anchor + 86400000000) == (True, '18:25')
    assert text(anchor - 1) == (False, '--:--')
    for bad in [b'', b'Mon, 19 Oct 2026 10:25:58', b'Mon, 19 Oct 2026 10:25:58 CST',
                b'Mon, 00 Oct 2026 10:25:58 GMT', b'Mon, 32 Oct 2026 10:25:58 GMT',
                b'Mon, 31 Apr 2026 10:25:58 GMT', b'Mon, 29 Feb 2025 10:25:58 GMT',
                b'Mon, 19 Oct 2026 24:25:58 GMT', b'Mon, 19 Oct 2026 10:60:58 GMT',
                b'Mon, 19 Oct 2026 10:25:60 GMT', b'Mon, 19 Oct 2023 10:25:58 GMT',
                b'Mon, 19 Foo 2026 10:25:58 GMT', b'Mon, 19 Oct 2026 1x:25:58 GMT']:
        assert not lib.nrf_clock_http_date(bad, anchor)
        assert text(anchor) == (True, '18:25'), 'bad header discarded a good clock'
    assert lib.nrf_clock_http_date(b'Thu, 29 Feb 2024 15:59:59 GMT', 1000000)
    assert text(3000000) == (True, '00:00')
    assert lib.nrf_clock_http_date(b'Mon, 19 Oct 2026 00:00:00 GMT', 1000000)
    assert text(1000000) == (True, '08:00'), 'timezone offset applied twice'
    tiny = C.create_string_buffer(b'ABCD')
    assert not lib.nrf_clock_text(tiny, 5, 1000000)
    assert tiny.value == b'ABCD'
    assert not lib.nrf_clock_http_date(b'Mon, 19 Oct 2026 00:00:00 GMT', -1)
    unload_library(lib._handle)
print('PASS: production UTC Date clock; +8 offset, rollover, monotonic wrap, leap-day '
      'validation, malformed headers retain last time, bounded output, unknown-time state')
