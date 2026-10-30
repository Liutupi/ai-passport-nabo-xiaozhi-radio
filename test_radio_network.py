"""Execute the real network_worker against a scripted, memory-only HTTP client.

No sockets, device access or real sleeps. The production worker, ring, Helix and
telemetry execute; only ESP HTTP/RTOS/Wi-Fi boundaries are substituted. IDF 6.0.2
maps receive timeouts to -ESP_ERR_HTTP_EAGAIN. Zero (including premature FIN) and
generic negative read errors are connection closures, not fabricated timeouts.
"""
import argparse
import ctypes as C
import importlib.util
import json
import hashlib
from pathlib import Path
import subprocess
import tempfile

from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
HOST = ROOT / 'tests/radio_host'
EAGAIN = -0x7007
COMPLETE, WIFI_DOWN, CANCEL, WIFI_UP, MEMORY_OK, SWITCH, DRAIN = 1, 2, 4, 8, 16, 32, 64
spec = importlib.util.spec_from_file_location('network_fixture', ROOT / 'tests/legacy/test_radio_decoder.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
PAYLOAD = fixture.FIXTURE * 64


class Telemetry(C.Structure):
    _fields_ = [(name, C.c_uint) for name in ('ring', 'rebuffers', 'rebuffer_ms', 'reconnects',
                                            'open_ms', 'heap_before_open', 'largest_before_open',
                                            'stall_reason')]


class NetworkTelemetry(C.Structure):
    _fields_ = [(name, C.c_uint) for name in (
        'reads', 'eagains', 'read_max_ms', 'read_inflight_ms', 'body_bytes',
        'body_age_ms', 'body_gap_max_ms', 'published_bytes', 'publish_age_ms',
        'ring_wait_ms', 'timeout_errors', 'task_state', 'runtime_us', 'stack')]


def compile_network(directory, source_override=None):
    vendor = BOARD / 'helix'
    sources = [path for path in sorted(vendor.glob('*.c')) if not path.name.startswith('._')]
    sources += [path for path in sorted((vendor / 'real').glob('*.c'))
                if path.name != 'buffers.c' and not path.name.startswith('._')]
    sources += [BOARD / name for name in ('nabo_helix_buffers.c', 'nabo_radio_decoder.c',
                                         'nabo_radio_dsp.c', 'nabo_radio_catalog.c', 'nabo_radio_clock.c',
                                         'nabo_radio_stream.c')]
    library = directory / 'network.dll'
    harness = HOST / 'network.c'
    if source_override:
        harness = directory / 'network.c'
        harness.write_bytes((HOST / 'network.c').read_bytes())
        before_source = source_override.read_text('utf-8')
        marker = '#ifndef NRF_ENGINE_HOST\ntypedef struct {\n    nabo_icy_t icy;'
        enabled_marker = '#if !defined(NRF_ENGINE_HOST) || defined(NRF_NETWORK_HOST)\ntypedef struct {\n    nabo_icy_t icy;'
        if before_source.count(marker) == 1:
            before_source = before_source.replace(marker, enabled_marker, 1)
        else:
            assert before_source.count(enabled_marker) == 1, 'preimage host macro location changed'
        (directory / 'nabo_radio.c').write_text(before_source, encoding='utf-8')
    subprocess.run([CC, '-std=gnu11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-but-set-variable', SHARED_EXPORT,
                    '-DNABO_HELIX_PORTABLE', '-DNRF_CLOCK_HOST', '-I', str(BOARD), '-I', str(HOST),
                    *(['-DNRF_NETWORK_BEFORE'] if source_override else []),
                    '-I', str(vendor / 'pub'), '-I', str(vendor / 'real'),
                    *map(str, sources), str(harness), '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    for name in ('host_net_reset', 'host_net_add_read', 'host_net_schedule', 'host_net_data',
                 'host_net_run', 'host_net_finish'):
        getattr(lib, name).restype = C.c_bool
    lib.host_net_connection.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int64,
                                         C.c_uint, C.c_uint, C.c_uint]
    lib.host_net_connection.restype = C.c_int
    lib.host_net_reset_station.argtypes = [C.c_uint, C.c_uint]
    lib.host_net_reset_station.restype = C.c_bool
    lib.host_net_add_read.argtypes = [C.c_uint, C.c_int, C.c_int, C.c_uint, C.c_uint]
    lib.host_net_data.argtypes = [C.c_void_p, C.c_uint]
    lib.host_net_seed.argtypes = [C.c_void_p, C.c_size_t]
    lib.host_net_seed.restype = C.c_size_t
    lib.host_net_decode.argtypes = [C.POINTER(C.c_int16), C.c_uint]
    lib.host_net_decode.restype = C.c_size_t
    lib.host_net_copy_ring.argtypes = [C.c_void_p, C.c_uint]
    lib.host_net_copy_ring.restype = C.c_uint
    lib.host_net_value.argtypes = [C.c_uint, C.c_uint]
    lib.host_net_value.restype = C.c_uint64
    lib.host_net_timeout_setup.argtypes = [C.c_uint, C.c_int, C.c_int, C.c_int]
    lib.host_net_timeout_setup.restype = C.c_bool
    lib.host_net_body_callbacks.argtypes = [C.c_uint, C.c_uint, C.c_uint]
    lib.host_net_body_callbacks.restype = C.c_bool
    lib.host_net_read_capacity.argtypes = [C.c_uint, C.c_uint]
    lib.host_net_read_capacity.restype = C.c_uint
    lib.host_net_read_snapshot.argtypes = [C.c_uint, C.c_uint, C.POINTER(NetworkTelemetry)]
    lib.host_net_read_snapshot.restype = C.c_bool
    lib.host_net_response_headers.argtypes = [C.c_uint, C.c_char_p, C.c_uint]
    lib.host_net_response_headers.restype = C.c_bool
    lib.nabo_radio_telemetry.argtypes = [C.POINTER(Telemetry)]
    if not source_override:
        lib.nabo_radio_network_telemetry.argtypes = [C.POINTER(NetworkTelemetry)]
    lib.nabo_radio_finished.restype = C.c_bool
    lib.nabo_radio_active.restype = C.c_bool
    lib.nabo_radio_finish.restype = None
    return lib


def value(lib, key, index=0):
    return lib.host_net_value(key, index)


def telemetry(lib):
    result = Telemetry()
    lib.nabo_radio_telemetry(C.byref(result))
    return result


def network_telemetry(lib):
    result = NetworkTelemetry()
    lib.nabo_radio_network_telemetry(C.byref(result))
    return result


def read_snapshot(lib, index, phase):
    result = NetworkTelemetry()
    assert lib.host_net_read_snapshot(index, phase, C.byref(result))
    return result


def connection(lib, *, init=True, opened=0, status=200, length=0, init_ms=0, open_ms=0, fetch_ms=0):
    index = lib.host_net_connection(init, opened, status, length, init_ms, open_ms, fetch_ms)
    assert index >= 0
    return index


def read(lib, index, result, ms=0, flags=0, error=0):
    assert lib.host_net_add_read(index, result, error, ms, flags)


def data(lib, payload):
    source = C.create_string_buffer(payload)
    assert lib.host_net_data(source, len(payload))


def queued(lib):
    result = C.create_string_buffer(32768)
    count = lib.host_net_copy_ring(result, len(result))
    return result.raw[:count]


def complete_run(lib, expected_connections, maximum_ms=60000):
    assert lib.host_net_run(maximum_ms), 'production worker did not mark done'
    assert value(lib, 7) == 0, 'script exhausted or virtual-time safety bound reached'
    assert value(lib, 1) == expected_connections
    assert value(lib, 11) == 0, 'HTTP handle leaked after worker finished'
    assert value(lib, 15) == 0, 'HTTP handle was cleaned twice'
    assert value(lib, 9) == value(lib, 10) == value(lib, 17) == 1, 'network reconnect replaced or mutated decoder/ring/task'


def check_short_gap(lib, gap_ms):
    assert lib.host_net_reset(8192)
    try:
        index = connection(lib, open_ms=181, fetch_ms=43)
        read(lib, index, 333, ms=101)
        remaining = gap_ms
        steps = 0
        while remaining:
            step = min(800, remaining)
            read(lib, index, EAGAIN, ms=step)
            remaining -= step
            steps += 1
        read(lib, index, 511, ms=53)
        read(lib, index, 0, flags=CANCEL)
        expected = bytes(range(256)) * 4
        data(lib, expected)
        complete_run(lib, 1)
        current = telemetry(lib)
        assert (current.reconnects, current.stall_reason) == (0, 0)
        assert current.open_ms == 325, 'open_ms must include open, headers and first body wait'
        assert current.heap_before_open > 0 and current.largest_before_open > 0
        assert queued(lib) == expected[:844], 'short HTTP chunks were lost or padded'
        assert value(lib, 0) == value(lib, 4) == 1
        assert value(lib, 3) >= steps + 3, 'positive chunks may split at the low-water read cap'
        stats = network_telemetry(lib)
        assert stats.reads == value(lib, 3) and stats.eagains == steps
        assert stats.body_bytes == stats.published_bytes == 844
        assert stats.read_inflight_ms == stats.timeout_errors == 0
    finally:
        assert lib.host_net_finish()


def check_long_stall_and_retention(lib):
    assert lib.host_net_reset(8192)
    try:
        source = C.create_string_buffer(PAYLOAD)
        assert lib.host_net_seed(source, 6144) == 6144
        pcm = (C.c_int16 * 480)()
        assert lib.host_net_decode(pcm, len(pcm)) == 480
        before = queued(lib)
        index = connection(lib)
        read(lib, index, 1024, ms=10)
        for _ in range(10):
            read(lib, index, EAGAIN, ms=800)
        second = connection(lib, open_ms=41, fetch_ms=17)
        read(lib, second, 257, ms=23)
        read(lib, second, 0, flags=CANCEL)
        data(lib, PAYLOAD[6144:6144+1281])
        complete_run(lib, 2)
        current = telemetry(lib)
        assert (current.reconnects, current.stall_reason) == (1, 1)
        assert current.open_ms == 81
        assert value(lib, 104, index) == 11, 'EAGAIN reconnected before the 8 s floor'
        assert value(lib, 101, second) - value(lib, 103, index) == 1000
        assert value(lib, 0) == value(lib, 4) == 2, 'each HTTP attempt must clean up once'
        assert queued(lib) == before + PAYLOAD[6144:6144+1281]
    finally:
        assert lib.host_net_finish()


def check_eof_and_errors(lib):
    cases = []
    for result, flags, error in ((0, 0, 0), (0, COMPLETE, 0), (-1, 0, 5)):
        assert lib.host_net_reset(8192)
        try:
            first = connection(lib)
            read(lib, first, 29, ms=10)
            read(lib, first, result, ms=20, flags=flags, error=error)
            second = connection(lib)
            read(lib, second, 0, flags=CANCEL)
            data(lib, b'x' * 29)
            complete_run(lib, 2)
            current = telemetry(lib)
            assert (current.reconnects, current.stall_reason) == (1, 2)
            assert value(lib, 104, first) == 2
            assert queued(lib) == b'x' * 29
            cases.append({'read_result': result, 'complete': bool(flags & COMPLETE)})
        finally:
            assert lib.host_net_finish()
    return cases


def check_open_retries_and_backoff(lib):
    assert lib.host_net_reset(8192)
    try:
        attempts = [connection(lib, opened=-1, status=0, length=-1) for _ in range(3)]
        attempts.append(connection(lib, status=503))
        success = connection(lib)
        read(lib, success, 64, ms=20)
        read(lib, success, 0, flags=COMPLETE)
        after_success = connection(lib)
        read(lib, after_success, 0, flags=CANCEL)
        data(lib, b'a' * 64)
        complete_run(lib, 6)
        backoff = [value(lib, 101, following) - value(lib, 103, previous)
                   for previous, following in zip(attempts, attempts[1:] + [success])]
        assert backoff == [1000, 2000, 4000, 8000], backoff
        assert value(lib, 101, after_success) - value(lib, 103, success) == 1000
        assert value(lib, 0) == value(lib, 4) == 6
        assert telemetry(lib).reconnects == 5
        assert queued(lib) == b'a' * 64
        return backoff
    finally:
        assert lib.host_net_finish()


def check_wifi_wait(lib):
    assert lib.host_net_reset(8192)
    try:
        first = connection(lib)
        read(lib, first, 128, ms=50)
        read(lib, first, 0, flags=WIFI_DOWN)
        second = connection(lib)
        read(lib, second, 0, flags=CANCEL)
        assert lib.host_net_schedule(5000, WIFI_UP)
        data(lib, b'w' * 128)
        complete_run(lib, 2)
        assert value(lib, 101, second) >= 5000, 'HTTP open was attempted while Wi-Fi was down'
        assert value(lib, 0) == value(lib, 4) == 2
        assert (telemetry(lib).reconnects, telemetry(lib).stall_reason) == (1, 3)
        assert queued(lib) == b'w' * 128
    finally:
        assert lib.host_net_finish()


def check_cancel_and_init_failure(lib):
    for wifi_down in (False, True):
        assert lib.host_net_reset(8192)
        try:
            if wifi_down:
                index = connection(lib)
                read(lib, index, 0, flags=WIFI_DOWN)
            else:
                connection(lib, opened=-1, status=0, length=-1)
            assert lib.host_net_schedule(200, CANCEL)
            complete_run(lib, 1)
            assert value(lib, 6) < 1000, 'cancel was delayed through an entire retry wait'
            assert value(lib, 0) == value(lib, 4) == 1
        finally:
            assert lib.host_net_finish()
    assert lib.host_net_reset(8192)
    try:
        connection(lib, init=False, init_ms=17)
        second = connection(lib)
        read(lib, second, 0, flags=CANCEL)
        complete_run(lib, 1)
        assert value(lib, 0) == 2 and value(lib, 4) == 1
        assert value(lib, 100, second) == 1017
        assert telemetry(lib).stall_reason == 4
    finally:
        assert lib.host_net_finish()


def check_preimage(lib):
    """Reproduce old-policy failures without changing its runtime behavior."""
    result = {}
    assert lib.host_net_reset(8192)
    try:
        first = connection(lib)
        read(lib, first, 333, ms=10)
        for _ in range(4):
            read(lib, first, EAGAIN, ms=750)
        read(lib, first, 511)
        read(lib, first, 0, flags=CANCEL)
        premature_retry = connection(lib)
        read(lib, premature_retry, 0, flags=CANCEL)
        data(lib, b'c' * 844)
        assert lib.host_net_run(10000)
        assert value(lib, 7) == 0
        assert value(lib, 104, first) < 7
        assert telemetry(lib).reconnects >= 1
        result['short_gap'] = {'result': 'FAIL_REPRODUCED', 'gap_script_ms': 3000,
                               'read_calls_before_reconnect': value(lib, 104, first),
                               'reconnects': telemetry(lib).reconnects}
    finally:
        assert lib.host_net_finish()
    assert lib.host_net_reset(8192)
    try:
        connection(lib, opened=-1, status=0, length=-1)
        later = connection(lib)
        read(lib, later, 0, flags=CANCEL)
        assert lib.host_net_run(10000)
        assert value(lib, 0) == 1 and value(lib, 14) == 6
        result['empty_ring_open_failure'] = {'result': 'FAIL_REPRODUCED',
                                             'attempts_started': value(lib, 0),
                                             'future_script_attempts_not_reached': 1,
                                             'worker_done': bool(value(lib, 12)),
                                             'state': 'FAILED'}
    finally:
        assert lib.host_net_finish()
    return result


def check_memory_wait(lib):
    for free_bytes, largest in ((8192, 8192), (65536, 2048)):
        assert lib.host_net_reset(8192)
        try:
            lib.host_net_heap(free_bytes, largest)
            index = connection(lib)
            read(lib, index, 0, flags=CANCEL)
            assert lib.host_net_schedule(3000, MEMORY_OK)
            complete_run(lib, 1)
            assert value(lib, 100, index) >= 3000, 'HTTP initialized while heap reserve was inadequate'
            assert value(lib, 0) == value(lib, 4) == 1
            assert telemetry(lib).stall_reason == 6
            assert value(lib, 18) == 0, 'recoverable wait requested pipeline release prematurely'
        finally:
            assert lib.host_net_finish()
    assert lib.host_net_reset(8192)
    try:
        lib.host_net_heap(8192, 2048)
        assert lib.host_net_schedule(200, CANCEL)
        complete_run(lib, 0)
        assert value(lib, 0) == value(lib, 4) == 0
        assert value(lib, 6) < 1000
        assert value(lib, 18) == 0
    finally:
        assert lib.host_net_finish()


def check_bounded_memory_release(lib, seed_kind):
    assert lib.host_net_reset(8192)
    try:
        if seed_kind == 'partial_header':
            prefix = C.create_string_buffer(b'\xff\xfb\x94')
            assert lib.host_net_seed(prefix, 3) == 3
        elif seed_kind == 'ready_pcm':
            source = C.create_string_buffer(PAYLOAD)
            assert lib.host_net_seed(source, 6144) == 6144
            first = (C.c_int16 * 1)()
            assert lib.host_net_decode(first, 1) == 1
            assert value(lib, 20) > 0, 'fixture must leave decoded PCM ready before waiting'
        lib.host_net_heap(8192, 2048)
        complete_run(lib, 0, maximum_ms=15000)
        assert value(lib, 6) == 10000, 'unrestored memory reserve must have a finite 10 s wait'
        assert value(lib, 0) == value(lib, 1) == value(lib, 4) == 0
        assert value(lib, 18) == 1, 'worker did not hand a memory-release request to its owner'
        assert telemetry(lib).stall_reason == 6
        assert lib.nabo_radio_active(), 'worker must leave deallocation to the speaker owner'
        # Normal owner reads drain empty/partial input or already-ready PCM,
        # then finish releases once and schedules allocation retry.
        pcm = (C.c_int16 * 480)()
        decoded = 0
        for _ in range(1000):
            if lib.nabo_radio_finished():
                break
            decoded += lib.host_net_decode(pcm, len(pcm))
        else:
            raise AssertionError(f'memory timeout did not drain owner input: {seed_kind}')
        assert (decoded > 0) == (seed_kind == 'ready_pcm')
        assert lib.nabo_radio_finished()
        lib.nabo_radio_finish()
        assert not lib.nabo_radio_active()
        assert value(lib, 18) == 0
        assert value(lib, 19) == 3000, 'memory release must use allocation backoff'
        lib.nabo_radio_finish()
        assert value(lib, 19) == 3000, 'duplicate finish advanced the retry deadline'
    finally:
        assert lib.host_net_finish()


def check_stream_timeout_setup(lib):
    """An invalid receive budget cannot silently become an active stream."""
    cases = []
    assert lib.host_net_reset(8192)
    try:
        index = connection(lib)
        assert lib.host_net_timeout_setup(index, 0, 0, 0)
        read(lib, index, 37, ms=112)
        read(lib, index, 0, flags=CANCEL)
        data(lib, b'z' * 37)
        complete_run(lib, 1)
        assert value(lib, 112, index) == value(lib, 113, index) == value(lib, 114, index) == 1
        assert value(lib, 106, index) == value(lib, 115, index) == 800
        assert queued(lib) == b'z' * 37, 'socket zero is valid and must permit playback'
        assert network_telemetry(lib).timeout_errors == 0
        cases.append({'case': 'valid_socket_zero', 'result': 'PASS'})
    finally:
        assert lib.host_net_finish()
    for name, fd, setter_result, socket_result in (
            ('client_timeout_setter_failure', 51, -1, 0),
            ('invalid_socket', -1, 0, 0),
            ('socket_receive_timeout_failure', 51, 0, -1)):
        assert lib.host_net_reset(8192)
        try:
            first = connection(lib)
            assert lib.host_net_timeout_setup(first, fd, setter_result, socket_result)
            # Any accidental read of this connection exhausts its script and fails.
            second = connection(lib)
            read(lib, second, 0, flags=CANCEL)
            complete_run(lib, 2)
            assert value(lib, 116, first) == 0, name
            assert value(lib, 105, first) == value(lib, 105, second) == 1
            assert value(lib, 101, second) - value(lib, 103, first) == 1000
            assert (telemetry(lib).reconnects, telemetry(lib).stall_reason) == (1, 7)
            stats = network_telemetry(lib)
            assert stats.timeout_errors == 1 and stats.reads == 1
            assert stats.body_bytes == stats.published_bytes == 0
            if fd < 0 or setter_result:
                assert value(lib, 114, first) == 0, 'must not configure an invalid transport'
            cases.append({'case': name, 'result': 'PASS', 'failed_attempt_body_reads': 0})
        finally:
            assert lib.host_net_finish()
    return {'scenario_count': len(cases), 'cases': cases,
            'stream_client_and_socket_receive_timeout_ms': 800}


def check_watermark_read_caps(lib):
    cases = []
    for capacity, seed_bytes, expected_caps in (
            (8192, 0, [256, 256, 256, 256]),
            (8192, 4095, [256, 1024]),
            (8192, 4096, [1024]),
            (16384, 4096, [1024])):
        assert lib.host_net_reset(capacity)
        try:
            prefix = bytes((i * 37) % 256 for i in range(seed_bytes))
            source = C.create_string_buffer(prefix)
            assert lib.host_net_seed(source, len(prefix)) == len(prefix)
            payload = bytes(range(256)) * 4
            index = connection(lib)
            read(lib, index, len(payload), ms=91)
            read(lib, index, 0, flags=CANCEL)
            data(lib, payload)
            complete_run(lib, 1)
            assert [lib.host_net_read_capacity(index, i) for i in range(len(expected_caps))] == expected_caps
            assert value(lib, 116, index) == len(expected_caps) + 1
            assert value(lib, 104, index) == 2, 'split reads must consume one ordered script item'
            assert value(lib, 6) == 91, 'splitting a positive step must not repeat its elapsed time'
            assert queued(lib) == prefix + payload
            stats = network_telemetry(lib)
            assert stats.body_bytes == stats.published_bytes == len(payload)
            cases.append({'ring_bytes': capacity, 'initial_ring_bytes': seed_bytes,
                          'data_read_request_bytes': expected_caps, 'payload_conserved': True})
        finally:
            assert lib.host_net_finish()
    return {'scenario_count': len(cases), 'cases': cases}


def check_callback_publication_timing(lib):
    """Body callbacks can precede the HTTP return; audio is queued once."""
    assert lib.host_net_reset(8192)
    try:
        index = connection(lib)
        assert lib.host_net_body_callbacks(index, 64, 4000)
        payload = bytes((i * 43) % 256 for i in range(640))
        read(lib, index, len(payload), ms=120)
        read(lib, index, 0, ms=25, flags=CANCEL)
        data(lib, payload)
        complete_run(lib, 1)
        arrived = read_snapshot(lib, index, 0)
        withheld = read_snapshot(lib, index, 1)
        assert arrived.body_bytes == withheld.body_bytes == 256
        assert arrived.published_bytes == withheld.published_bytes == 0
        assert arrived.read_inflight_ms == 120 and withheld.read_inflight_ms == 4120
        assert arrived.body_age_ms == 0 and withheld.body_age_ms == 4000
        current = network_telemetry(lib)
        assert current.reads == 4 and current.eagains == 0
        assert current.read_max_ms == 4120 and current.read_inflight_ms == 0
        assert current.body_bytes == current.published_bytes == len(payload)
        assert current.body_age_ms == current.publish_age_ms == 25
        assert current.body_gap_max_ms == 4000
        assert value(lib, 117, index) == 10 and value(lib, 118, index) == len(payload)
        assert queued(lib) == payload, 'ON_DATA diagnostics must never enqueue duplicate audio'
        return {'scenario_count': 1, 'body_callback_fragment_bytes': 64,
                'scripted_read_tail_delay_ms': 4000, 'read_max_ms': current.read_max_ms,
                'body_bytes': current.body_bytes, 'published_bytes': current.published_bytes,
                'body_callback_precedes_publication': True,
                'boundary_is_scripted_not_real_tls': True}
    finally:
        assert lib.host_net_finish()


def check_icy_and_invalid_headers(lib):
    assert lib.host_net_reset(8192)
    try:
        index = connection(lib)
        assert lib.host_net_response_headers(index, b'audio/mpeg', 5)
        assert lib.host_net_body_callbacks(index, 4, 0)
        wire = b'ABCDE' + bytes([1]) + b'metadata'.ljust(16, b'\x00') + b'FGHIJ\x00KLM'
        read(lib, index, len(wire), ms=13)
        read(lib, index, 0, flags=CANCEL)
        data(lib, wire)
        complete_run(lib, 1)
        current = network_telemetry(lib)
        assert current.body_bytes == len(wire) and current.published_bytes == 13
        assert queued(lib) == b'ABCDEFGHIJKLM', 'body telemetry must include stripped metadata without publishing it'
    finally:
        assert lib.host_net_finish()
    assert lib.host_net_reset(8192)
    try:
        invalid = connection(lib)
        assert lib.host_net_response_headers(invalid, b'text/html', 0)
        second = connection(lib)
        read(lib, second, 0, flags=CANCEL)
        complete_run(lib, 2)
        assert value(lib, 116, invalid) == value(lib, 113, invalid) == value(lib, 114, invalid) == 0
        assert network_telemetry(lib).body_bytes == network_telemetry(lib).published_bytes == 0
    finally:
        assert lib.host_net_finish()
    return {'scenario_count': 2, 'icy_raw_body_and_audio_publication_separate': True,
            'invalid_content_type_has_no_body_reads': True}


def check_ring_backpressure_and_stats_reset(lib):
    assert lib.host_net_reset(8192)
    try:
        prefix = b'r' * 8192
        source = C.create_string_buffer(prefix)
        assert lib.host_net_seed(source, len(prefix)) == len(prefix)
        index = connection(lib)
        read(lib, index, 31)
        data(lib, b'n' * 31)
        assert lib.host_net_schedule(50, CANCEL)
        complete_run(lib, 1)
        stats = network_telemetry(lib)
        assert stats.reads == 1 and stats.body_bytes == 31
        assert stats.published_bytes == 0 and stats.ring_wait_ms == 50
        assert queued(lib) == prefix, 'a full ring must retain existing compressed data'
    finally:
        assert lib.host_net_finish()
    assert network_telemetry(lib).body_bytes == 31, 'finish retains generation diagnostics for review'
    assert lib.host_net_reset(8192)
    try:
        stats = network_telemetry(lib)
        assert all(getattr(stats, field) == 0 for field, _ in NetworkTelemetry._fields_), 'new selection/play generation must reset network diagnostics'
        index = connection(lib)
        read(lib, index, 0, flags=CANCEL)
        complete_run(lib, 1)
    finally:
        assert lib.host_net_finish()
    return {'scenario_count': 2, 'full_ring_wait_ms': 50,
            'unpublished_body_is_not_counted_as_music': True,
            'selection_play_generation_resets_stats': True}


def eagain_gap(lib, index, gap_ms):
    while gap_ms:
        step = min(800, gap_ms)
        read(lib, index, EAGAIN, ms=step)
        gap_ms -= step


def check_source_silence_grace(lib):
    cases = []
    assert lib.host_net_reset_station(8192, 123)
    try:
        index = connection(lib)
        read(lib, index, 19, ms=10)
        eagain_gap(lib, index, 15000)
        read(lib, index, 23)
        eagain_gap(lib, index, 15000)
        read(lib, index, 31)
        read(lib, index, 0, flags=CANCEL)
        data(lib, b'a' * 19 + b'b' * 23 + b'c' * 31)
        complete_run(lib, 1)
        assert value(lib, 6) == 30010 and telemetry(lib).reconnects == 0
        assert queued(lib) == b'a' * 19 + b'b' * 23 + b'c' * 31
        assert network_telemetry(lib).eagains == 38
        cases.append({'case': 'qtfm_two_15s_gaps_same_connection', 'result': 'PASS',
                      'audio_body_resets_connection_deadline': True})
    finally:
        assert lib.host_net_finish()
    for qt, floor in ((123, 20000), (0, 8000)):
        assert lib.host_net_reset_station(8192, qt)
        try:
            first = connection(lib)
            read(lib, first, 17, ms=11)
            eagain_gap(lib, first, floor - 1)
            read(lib, first, EAGAIN, ms=1)
            # No first audio on the next connection. It must receive its full
            # local grace despite the old connection's already-expired clock.
            second = connection(lib, open_ms=123, fetch_ms=77)
            eagain_gap(lib, second, floor - 1)
            read(lib, second, EAGAIN, ms=1)
            third = connection(lib)
            read(lib, third, 0, flags=CANCEL)
            data(lib, b'd' * 17)
            complete_run(lib, 3)
            assert value(lib, 103, first) == 11 + floor
            assert value(lib, 103, second) - value(lib, 102, second) == floor
            assert value(lib, 101, second) - value(lib, 103, first) == 1000
            assert value(lib, 101, third) - value(lib, 103, second) == 2000
            assert telemetry(lib).reconnects == 2 and telemetry(lib).stall_reason == 1
            assert queued(lib) == b'd' * 17
            cases.append({'case': 'qtfm' if qt else 'generic', 'result': 'PASS',
                          'no_audio_floor_ms': floor, 'deadline_resets_each_connection': True})
        finally:
            assert lib.host_net_finish()
    assert lib.host_net_reset_station(8192, 123)
    try:
        first = connection(lib)
        assert lib.host_net_response_headers(first, b'audio/mpeg', 1)
        read(lib, first, 1, ms=10)
        # One valid audio byte, a metadata-length byte, then a slowly arriving
        # metadata block. Positive HTTP bodies alone must not extend audio life.
        read(lib, first, 1)
        for _ in range(40):
            read(lib, first, 1, ms=500)
        second = connection(lib)
        read(lib, second, 0, flags=CANCEL)
        data(lib, b'A\xff' + b'm' * 40)
        complete_run(lib, 2)
        assert value(lib, 103, first) == 20010
        stats = network_telemetry(lib)
        assert stats.eagains == 0 and stats.body_bytes == 42 and stats.published_bytes == 1
        assert queued(lib) == b'A'
        assert telemetry(lib).reconnects == 1 and telemetry(lib).stall_reason == 1
        cases.append({'case': 'metadata_only_does_not_reset_audio_deadline', 'result': 'PASS',
                      'raw_body_bytes': 42, 'audio_published_bytes': 1})
    finally:
        assert lib.host_net_finish()
    for flags, positive in ((CANCEL, False), (SWITCH, False), (SWITCH, True)):
        assert lib.host_net_reset_station(8192, 123)
        try:
            index = connection(lib)
            read(lib, index, 31 if positive else EAGAIN, ms=800, flags=flags)
            if positive:
                data(lib, b'o' * 31)
            complete_run(lib, 1)
            assert value(lib, 6) == 800 and telemetry(lib).reconnects == 0
            assert queued(lib) == b'', 'cancelled/old-generation read published stale audio'
            assert network_telemetry(lib).published_bytes == 0
            assert bool(value(lib, 8)) == (flags == CANCEL)
            cases.append({'case': 'switch' if flags == SWITCH else 'cancel', 'result': 'PASS',
                          'positive_old_generation_body': positive, 'stop_ms': 800})
        finally:
            assert lib.host_net_finish()
    return {'scenario_count': len(cases), 'cases': cases,
            'qtfm_identified_by_catalog_qt_nonzero': True,
            'http_open_timeout_ms_unchanged': 5000, 'receive_timeout_ms_unchanged': 800,
            'cross_connection_body_gap_is_not_timeout_input': True}


def check_long_backpressure_and_wifi(lib):
    cases = []
    assert lib.host_net_reset(8192)
    try:
        prefix = C.create_string_buffer(b'r' * 8192)
        assert lib.host_net_seed(prefix, 8192) == 8192
        index = connection(lib)
        read(lib, index, 31)
        eagain_gap(lib, index, 7999)
        read(lib, index, 1)
        read(lib, index, 0, flags=CANCEL)
        data(lib, b'n' * 31 + b'z')
        assert lib.host_net_schedule(22000, DRAIN)
        complete_run(lib, 1)
        stats = network_telemetry(lib)
        assert value(lib, 6) == 29999 and stats.ring_wait_ms == 22000
        assert stats.published_bytes == 32 and telemetry(lib).reconnects == 0
        assert queued(lib) == b'n' * 31 + b'z'
        cases.append({'case': '22s_full_ring_is_backpressure_not_source_silence', 'result': 'PASS',
                      'ring_wait_ms': 22000, 'following_source_wait_ms': 7999})
    finally:
        assert lib.host_net_finish()
    for seeded in (0, 8192):
        assert lib.host_net_reset(8192)
        try:
            prefix = C.create_string_buffer(b'r' * seeded)
            assert lib.host_net_seed(prefix, seeded) == seeded
            index = connection(lib)
            read(lib, index, 31, flags=WIFI_DOWN)
            data(lib, b'n' * 31)
            assert lib.host_net_schedule(50, CANCEL)
            complete_run(lib, 1)
            stats = network_telemetry(lib)
            assert value(lib, 103, index) == 0 and value(lib, 6) == 50
            assert stats.body_bytes == 31 and stats.ring_wait_ms == 0
            assert stats.published_bytes == (0 if seeded else 31)
            assert queued(lib) == (b'r' * seeded if seeded else b'n' * 31)
            assert telemetry(lib).stall_reason == 3
            cases.append({'case': 'wifi_down_full_ring' if seeded else 'wifi_down_room_available',
                          'result': 'PASS', 'received_audio_published_bytes': stats.published_bytes,
                          'bounded_unpublished_remainder_bytes': 31 - stats.published_bytes})
        finally:
            assert lib.host_net_finish()
    return {'scenario_count': len(cases), 'cases': cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--before-source', type=Path,
                        help='Optional sealed candidate06 source for behavior-preserving failure control')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temporary:
        lib = compile_network(Path(temporary))
        try:
            check_short_gap(lib, 3000)
            check_short_gap(lib, 7000)
            check_long_stall_and_retention(lib)
            closures = check_eof_and_errors(lib)
            backoff = check_open_retries_and_backoff(lib)
            check_wifi_wait(lib)
            check_cancel_and_init_failure(lib)
            check_memory_wait(lib)
            for seed_kind in ('empty', 'partial_header', 'ready_pcm'):
                check_bounded_memory_release(lib, seed_kind)
            stream_timeout = check_stream_timeout_setup(lib)
            watermark_caps = check_watermark_read_caps(lib)
            callback_timing = check_callback_publication_timing(lib)
            header_callbacks = check_icy_and_invalid_headers(lib)
            backpressure_reset = check_ring_backpressure_and_stats_reset(lib)
            silence_grace = check_source_silence_grace(lib)
            long_backpressure = check_long_backpressure_and_wifi(lib)
        finally:
            unload_library(lib._handle)
    before = None
    if args.before_source:
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temporary:
            lib = compile_network(Path(temporary), source_override=args.before_source)
            try:
                before = check_preimage(lib)
                before['source_sha256'] = hashlib.sha256(args.before_source.read_bytes()).hexdigest()
                before['only_test_enable_macro_changed'] = True
            finally:
                unload_library(lib._handle)
    report = {'result': 'PASS', 'evidence': 'host_scripted_real_network_worker',
              'hardware_or_live_network_test': False, 'eagain_gap_same_connection_ms': [3000, 7000],
              'eagain_stall_floor_ms': 8000, 'retry_backoff_ms': backoff,
              'closures': closures, 'http_attempts_cleaned_once': True,
              'queued_bytes_decoder_task_preserved': True, 'wifi_wait_and_cancel': True,
              'heap_wait_and_cancel': True, 'memory_wait_bound_ms': 10000,
              'owner_memory_release_once': True, 'first_allocation_retry_ms': 3000,
              'memory_release_drain_inputs': ['empty', 'partial_header', 'ready_pcm'],
              'stream_timeout_setup': stream_timeout, 'watermark_read_caps': watermark_caps,
              'body_callback_publication_timing': callback_timing,
              'response_header_callbacks': header_callbacks,
              'ring_backpressure_and_stats_reset': backpressure_reset,
              'source_silence_grace': silence_grace,
              'long_backpressure_and_wifi': long_backpressure,
              'network_telemetry_abi_bytes': C.sizeof(NetworkTelemetry),
              'legacy_telemetry_abi_bytes': C.sizeof(Telemetry),
              'scenario_count': 17 + sum(group['scenario_count'] for group in
                  (stream_timeout, watermark_caps, callback_timing, header_callbacks, backpressure_reset,
                   silence_grace, long_backpressure)),
              'before_control': before}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: real network_worker; 3/7 s EAGAIN keeps connection, 8 s stall recovery, '
          'EOF/read errors, 1/2/4/8 s retry/reset, Wi-Fi wait, cancellation, exact queued '
          'bytes/decoder/task retention, heap wait, client/socket timeout failure handling, '
          'low-water read caps, separate body/publication timing, QTFM 20 s vs generic 8 s '
          'audio grace, metadata-only expiry, long backpressure and generation reset (host only)')


if __name__ == '__main__':
    main()
