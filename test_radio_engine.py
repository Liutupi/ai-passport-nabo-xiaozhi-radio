"""Actual production compressed-buffer/decoder/DSP/stop lifecycle, offline MP3.

HTTP, I2S and NVS are stubbed at the boundary; this is not network or device QA.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from host_tools import CC, SHARED_EXPORT, unload_library
ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
spec=importlib.util.spec_from_file_location('fixture',ROOT/'tests/legacy/test_radio_decoder.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
payload=fixture.FIXTURE*32
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temp:
    dll=Path(temp)/'engine.dll'
    vendor=BOARD/'helix'
    sources=[f for f in sorted(vendor.glob('*.c')) if not f.name.startswith('._')]
    sources += [f for f in sorted((vendor/'real').glob('*.c'))
                if f.name != 'buffers.c' and not f.name.startswith('._')]
    subprocess.run([CC,'-std=gnu11','-shared','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-but-set-variable',
                    SHARED_EXPORT,'-DNABO_HELIX_PORTABLE','-I',str(BOARD),'-I',str(ROOT/'tests/radio_host'),'-I',str(vendor/'pub'),'-I',str(vendor/'real'),
                    *[str(f) for f in sources],*[str(BOARD/f) for f in ['nabo_helix_buffers.c','nabo_radio_decoder.c','nabo_radio_dsp.c','nabo_radio_catalog.c']],
                    str(ROOT/'tests/radio_host/engine.c'),'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll))
    for fn in ['nabo_radio_begin','nabo_radio_finished','host_clean','nabo_radio_wanted','nabo_radio_active','nabo_radio_favorite']:
        getattr(lib,fn).restype=C.c_bool
    lib.nabo_radio_read.argtypes=[C.POINTER(C.c_int16),C.c_size_t,C.c_int]
    lib.nabo_radio_read.restype=C.c_size_t
    assert lib.nabo_radio_clock_seconds()==60
    for seconds in [0,180,300,600,60]:
        lib.nabo_radio_set_clock_seconds(seconds)
        assert lib.nabo_radio_clock_seconds()==seconds
        assert lib.host_stored_clock()==seconds
    lib.nabo_radio_set_clock_seconds(1)
    assert lib.nabo_radio_clock_seconds()==60 and lib.host_stored_clock()==60
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
    # Fragmented heaps can reject a 32 KiB allocation. Both smaller rings must
    # prefill and stream the same samples without waiting for an impossible size.
    for capacity in [16384,8192]:
        lib.host_limit_ring(capacity)
        lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
        assert lib.host_ring_capacity()==capacity
        lib.host_wrap_positions()
        offset=0;audio=[];eof=False
        for _ in range(100000):
            if offset<len(payload):
                block=C.create_string_buffer(payload[offset:offset+2048])
                offset+=lib.host_feed(block,min(2048,len(payload)-offset))
            elif not eof:
                lib.host_eof();eof=True
            if lib.nabo_radio_finished():break
            dst=(C.c_int16*320)();n=lib.nabo_radio_read(dst,320,0);audio.extend(dst[:n])
        else:raise AssertionError('small ring stalled')
        assert audio==expected,capacity
        lib.nabo_radio_stop();lib.nabo_radio_finish();assert lib.host_clean()
    lib.host_limit_ring(0)
    lib.nabo_radio_select_station(0);assert not lib.nabo_radio_begin();assert lib.host_clean()
    lib.host_limit_ring(32768)
    lib.host_fail_ring_after(1)
    lib.nabo_radio_select_station(0);assert not lib.nabo_radio_begin();assert lib.host_clean()
    lib.host_fail_ring_after(-1)
    # Match the connected C3's ~30 KiB before allocating compressed data.
    # The old 16 KiB ring left too little for HTTP/TCP; choose 8 KiB instead.
    lib.host_heap_budget(30500,7680)
    lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
    assert lib.host_ring_capacity()==8192
    lib.nabo_radio_stop();lib.host_eof();lib.nabo_radio_finish();assert lib.host_clean()
    # Account for allocator metadata after allocation, and reject a fragmented
    # heap even if its total byte count looks sufficient. Failed rings unwind.
    for available,largest in [(24576,7680),(256*1024,2048)]:
        lib.host_heap_budget(available,largest)
        lib.nabo_radio_select_station(0);assert not lib.nabo_radio_begin()
        assert lib.host_clean()
    lib.host_heap_budget(256*1024,256*1024)
    # A real network stall must rebuild a cushion, not alternate tiny decoded
    # bursts and silence. Keep partial MP3 input/reservoir byte-exact across it.
    lib.host_limit_ring(8192)
    lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
    src=C.create_string_buffer(payload)
    offset=lib.host_feed(src,8192);audio=[]
    for _ in range(10000):
        dst=(C.c_int16*480)();n=lib.nabo_radio_read(dst,480,0);audio.extend(dst[:n])
        if not n:break
    else:raise AssertionError('starvation not reached')
    assert lib.nabo_radio_state()==2
    offset+=lib.host_feed(C.byref(src,offset),1024)
    assert lib.nabo_radio_read(dst,480,0)==0,'resumed without a recovery cushion'
    eof=False
    for _ in range(10000):
        if offset<len(payload):offset+=lib.host_feed(C.byref(src,offset),len(payload)-offset)
        elif not eof:lib.host_eof();eof=True
        if lib.nabo_radio_finished():break
        n=lib.nabo_radio_read(dst,480,0);audio.extend(dst[:n])
    else:raise AssertionError('rebuffered stream did not drain')
    assert audio==expected,'lost compressed input or decoder state on starvation'
    lib.nabo_radio_stop();lib.nabo_radio_finish();assert lib.host_clean()
    lib.host_limit_ring(32768)
    # Failed HTTP requests can finish before an MP3 header (or in an ID3 tag).
    # They must free the pipeline and retry, never stay active silently forever.
    for prefix in [b'', b'\xff\xfb\x94', b'ID3\x04\x00\x00\x00\x00\x20\x00xx']:
        lib.nabo_radio_select_station(0);assert lib.nabo_radio_begin()
        if prefix:
            fragment=C.create_string_buffer(prefix);assert lib.host_feed(fragment,len(prefix))==len(prefix)
        lib.host_eof();dst=(C.c_int16*480)()
        for _ in range(4):
            if lib.nabo_radio_finished():break
            assert lib.nabo_radio_read(dst,480,0)==0
        assert lib.nabo_radio_finished(),'EOF before first frame never finished'
        lib.nabo_radio_stop();lib.nabo_radio_finish();assert lib.host_clean()
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
    # ctypes releases the GIL for these C calls: exercise real concurrent
    # producer/consumer publication through the production segmented ring.
    lib.host_probe_begin.restype=C.c_bool
    for fn in ['host_probe_write','host_probe_read']:
        getattr(lib,fn).argtypes=[C.c_void_p,C.c_size_t]
        getattr(lib,fn).restype=C.c_size_t
    pattern=bytes(range(256))*4096
    source=C.create_string_buffer(pattern)
    for capacity in [8192,16384,32768]:
        assert lib.host_probe_begin(capacity)
        received=bytearray()
        def producer():
            at=0
            while at<len(pattern):
                n=lib.host_probe_write(C.byref(source,at),min(997,len(pattern)-at))
                at+=n
                if not n:time.sleep(.0001)
        def consumer():
            dst=(C.c_uint8*701)()
            while len(received)<len(pattern):
                n=lib.host_probe_read(dst,len(dst));received.extend(dst[:n])
                if not n:time.sleep(.0001)
        threads=[threading.Thread(target=fn,daemon=True) for fn in [producer,consumer]]
        for t in threads:t.start()
        for t in threads:t.join(10)
        assert not any(t.is_alive() for t in threads),'concurrent ring stalled'
        assert received==pattern
        lib.host_probe_end();assert lib.host_clean()
    unload_library(lib._handle)
    print(f'PASS: production segmented ring -> Helix -> half-band DSP; {len(expected)} exact samples at 5 read sizes and 8/16/32 KiB rings; 3 MiB concurrent transfer, position wrap, partial-allocation cleanup, EOF drain, pause tail, 50 clean cycles, 1105-station favorites and sleep expiry')
