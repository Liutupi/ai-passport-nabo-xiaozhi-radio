"""Actual production compressed-buffer/decoder/DSP/stop lifecycle, offline MP3.

HTTP, I2S and NVS are stubbed at the boundary; this is not network or device QA.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import subprocess
import tempfile
from host_tools import CC, SHARED_EXPORT, unload_library
ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
spec=importlib.util.spec_from_file_location('fixture',ROOT/'tests/legacy/test_radio_decoder.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
payload=fixture.FIXTURE*32
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temp:
    dll=Path(temp)/'engine.dll'
    vendor=BOARD/'helix'
    sources=sorted(vendor.glob('*.c'))+[f for f in sorted((vendor/'real').glob('*.c')) if f.name!='buffers.c']
    subprocess.run([CC,'-std=gnu11','-shared','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-but-set-variable',
                    SHARED_EXPORT,'-DNABO_HELIX_PORTABLE','-I',str(BOARD),'-I',str(ROOT/'tests/radio_host'),'-I',str(vendor/'pub'),'-I',str(vendor/'real'),
                    *[str(f) for f in sources],*[str(BOARD/f) for f in ['nabo_helix_buffers.c','nabo_radio_decoder.c','nabo_radio_dsp.c','nabo_radio_catalog.c']],
                    str(ROOT/'tests/radio_host/engine.c'),'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll))
    for fn in ['nabo_radio_begin','nabo_radio_finished','host_clean','nabo_radio_wanted','nabo_radio_active','nabo_radio_favorite']:
        getattr(lib,fn).restype=C.c_bool
    lib.nabo_radio_read.argtypes=[C.POINTER(C.c_int16),C.c_size_t,C.c_int]
    lib.nabo_radio_read.restype=C.c_size_t
    lib.host_feed.argtypes=[C.c_void_p,C.c_size_t];lib.host_feed.restype=C.c_size_t
    def session(chunk):
        lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
        buf=C.create_string_buffer(payload);assert lib.host_feed(buf,len(payload))==len(payload)
        lib.host_eof();audio=[]
        for _ in range(100000):
            if lib.nabo_radio_finished():break
            dst=(C.c_int16*chunk)();n=lib.nabo_radio_read(dst,chunk,0);audio.extend(dst[:n])
        else:raise AssertionError('stream did not drain')
        assert len(audio)>50000 and any(audio)
        assert lib.nabo_radio_output_rate()==24000
        lib.nabo_radio_stop();lib.nabo_radio_finish();assert lib.host_clean()
        return audio
    expected=session(320)
    for n in [1,97,441,480]:assert session(n)==expected,n
    # Pausing fades the tail to zero and frees every pipeline allocation.
    lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
    src=C.create_string_buffer(payload);lib.host_feed(src,len(payload));dst=(C.c_int16*480)()
    lib.nabo_radio_read(dst,480,0);lib.nabo_radio_stop();lib.host_eof()
    for _ in range(100):
        if lib.nabo_radio_finished():break
        lib.nabo_radio_read(dst,480,0)
    assert lib.nabo_radio_finished();lib.nabo_radio_finish();assert lib.host_clean()
    # Favorites above bit 63 and a zero timer stay bounded with the full catalog.
    cat=(BOARD/'radio_assets/radio_catalog.bin').read_bytes();data=C.create_string_buffer(cat)
    lib.nabo_radio_bind_catalog.argtypes=[C.c_void_p,C.c_size_t];lib.nabo_radio_bind_catalog.restype=C.c_bool
    assert lib.nabo_radio_bind_catalog(data,len(cat))
    for i in [0,64,1104]:lib.nabo_radio_toggle_favorite(i);assert lib.nabo_radio_favorite(i)
    lib.nabo_radio_set_sleep(1);lib.host_advance(61000);assert not lib.nabo_radio_wanted()
    for _ in range(50):
        lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin();lib.nabo_radio_stop();lib.host_eof();lib.nabo_radio_finish();assert lib.host_clean()
    unload_library(lib._handle)
    print(f'PASS: production compressed ring -> Helix -> half-band DSP; {len(expected)} exact samples at 5 read sizes; EOF drain, pause tail, 50 clean cycles, 1105-station favorites and sleep expiry')
