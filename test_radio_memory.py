"""Real pipeline/Helix allocations on a bounded coalescing first-fit host heap.

The measured device had about 76,100 free bytes and an 18,432-byte largest
block before decode allocation. The profiles below model that free-space
envelope with several plausible holes; they are not a captured device map.
Every pipeline malloc/calloc/free, including Helix, ring metadata/blocks and
the 8 KiB task stack, uses the same arena. Host pointer sizes and a documented
16-byte allocator header differ from ESP-IDF's allocator. No live HTTP, RTOS
task, serial device or physical acceptance is involved.
"""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
HOST = ROOT / 'tests/radio_host'
spec = importlib.util.spec_from_file_location('memory_fixture', ROOT / 'tests/legacy/test_radio_decoder.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
PAYLOAD = fixture.FIXTURE * 32
HOLES = [
    [18432, 16384, 7680, 7680, 7680, 7680, 6144, 4352],
    [18432, 15872, 7936, 7680, 7680, 7680, 6144, 4608],
    [18432, 16384, 8192, 8192, 7680, 7680, 6144, 4096],
    [131072],
    [16384, 18432, 7680, 7680, 7680, 7680, 6144, 4352],
]


def compile_memory(directory, before_source=None):
    vendor = BOARD / 'helix'
    sources = [path for path in sorted(vendor.glob('*.c')) if not path.name.startswith('._')]
    sources += [path for path in sorted((vendor / 'real').glob('*.c'))
                if path.name != 'buffers.c' and not path.name.startswith('._')]
    sources += [BOARD / name for name in ('nabo_helix_buffers.c', 'nabo_radio_decoder.c',
                                         'nabo_radio_dsp.c', 'nabo_radio_catalog.c')]
    harness = HOST / 'fragmented_heap.c'
    if before_source:
        harness = directory / 'fragmented_heap.c'
        harness.write_bytes((HOST / 'fragmented_heap.c').read_bytes())
        (directory / 'nabo_radio.c').write_bytes(before_source.read_bytes())
    library = directory / 'memory.dll'
    subprocess.run([CC, '-std=gnu11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-but-set-variable', SHARED_EXPORT,
                    '-DNABO_HELIX_PORTABLE', '-Dmalloc=fheap_malloc', '-Dcalloc=fheap_calloc',
                    '-Dfree=fheap_free', '-include', str(HOST / 'fragmented_heap.h'),
                    '-I', str(BOARD), '-I', str(HOST), '-I', str(vendor / 'pub'),
                    '-I', str(vendor / 'real'), *map(str, sources), str(harness),
                    '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    for name in ('host_memory_configure', 'host_memory_begin', 'host_memory_finished',
                 'host_memory_clean', 'host_memory_finish'):
        getattr(lib, name).restype = C.c_bool
    lib.host_memory_feed.argtypes = [C.c_void_p, C.c_size_t]
    lib.host_memory_feed.restype = C.c_size_t
    lib.host_memory_read.argtypes = [C.POINTER(C.c_int16), C.c_size_t]
    lib.host_memory_read.restype = C.c_size_t
    lib.host_memory_stat.argtypes = [C.c_uint]
    lib.host_memory_stat.restype = C.c_uint64
    lib.host_memory_allocation.argtypes = [C.c_uint, C.c_uint]
    lib.host_memory_allocation.restype = C.c_uint64
    lib.host_memory_aac_capacity.argtypes = [C.c_uint]
    lib.host_memory_aac_capacity.restype = C.c_int
    return lib


def stat(lib, key):
    return lib.host_memory_stat(key)


def allocations(lib):
    names = ('requested', 'success', 'free_before', 'largest_before', 'live_after', 'injected')
    return [{name: lib.host_memory_allocation(index, field) for field, name in enumerate(names)}
            for index in range(stat(lib, 7))]


def ready_stats(lib):
    return {'free_bytes': stat(lib, 2), 'largest_free_block': stat(lib, 3),
            'pipeline_live_reserved_bytes': stat(lib, 4), 'pipeline_peak_reserved_bytes': stat(lib, 5),
            'live_allocations': stat(lib, 6), 'allocation_attempts': stat(lib, 7),
            'max_requested_allocation': stat(lib, 11), 'ring_bytes': stat(lib, 13),
            'decode_state_bytes': stat(lib, 14), 'subband_bytes': stat(lib, 15),
            'minimum_free_during_begin': stat(lib, 16), 'host_pointer_bits': stat(lib, 18)}


def drain(lib, chunk=320):
    source = C.create_string_buffer(PAYLOAD)
    samples = (C.c_int16 * chunk)()
    at = 0
    eof = False
    audio = []
    for _ in range(100000):
        if at < len(PAYLOAD):
            at += lib.host_memory_feed(C.byref(source, at), min(2048, len(PAYLOAD) - at))
        elif not eof:
            lib.host_memory_eof()
            eof = True
        if lib.host_memory_finished():
            break
        count = lib.host_memory_read(samples, chunk)
        audio.extend(samples[:count])
    else:
        raise AssertionError('bounded-heap stream never drained')
    assert at == len(PAYLOAD) and len(audio) > 50000 and any(audio)
    return audio


def session(lib, profile, chunk=320):
    assert lib.host_memory_configure(profile)
    assert stat(lib, 0) == sum(HOLES[profile]) and stat(lib, 1) == max(HOLES[profile])
    assert lib.host_memory_begin(), (profile, allocations(lib))
    try:
        snapshot = ready_stats(lib)
        assert snapshot['ring_bytes'] in (8192, 16384, 32768)
        assert snapshot['free_bytes'] >= 16384
        assert snapshot['largest_free_block'] >= 4096
        audio = drain(lib, chunk)
        return audio, snapshot, allocations(lib)
    finally:
        assert lib.host_memory_finish(), 'pipeline did not restore original heap geometry'


def check_cycles(lib, expected):
    assert lib.host_memory_configure(0)
    peak = 0
    for cycle in range(50):
        lib.host_memory_trace_reset()
        assert lib.host_memory_begin(), cycle
        try:
            assert drain(lib) == expected, ('PCM mismatch', cycle)
            peak = max(peak, stat(lib, 5))
        finally:
            assert lib.host_memory_finish(), ('heap not reusable', cycle)
        assert stat(lib, 4) == stat(lib, 6) == stat(lib, 10) == 0
    return peak


def check_failure_injection(lib, expected, normal_attempts):
    result = []
    for fail_at in range(1, normal_attempts + 1):
        assert lib.host_memory_configure(0)
        lib.host_memory_fail_at(fail_at)
        started = lib.host_memory_begin()
        try:
            assert stat(lib, 9) == 1, ('allocation injection was not reached', fail_at)
            trace = allocations(lib)
            injected = [item for item in trace if item['injected']]
            assert len(injected) == 1 and not injected[0]['success']
            if started:
                # A failed larger-ring attempt may legitimately recover using
                # the unchanged 16/8 KiB fallback policy.
                assert drain(lib) == expected, ('fallback PCM mismatch', fail_at)
            else:
                assert lib.host_memory_clean(), ('partial failure leaked', fail_at)
            result.append({'allocation': fail_at, 'requested': injected[0]['requested'],
                           'begin_succeeded_via_fallback': started})
        finally:
            assert lib.host_memory_finish(), ('failure cleanup leaked', fail_at)
        lib.host_memory_trace_reset()
        assert lib.host_memory_begin(), ('heap not reusable after injected failure', fail_at)
        try:
            assert drain(lib) == expected
        finally:
            assert lib.host_memory_finish()
    return result


def check_aac_capacity(lib):
    assert lib.host_memory_configure(0)
    assert lib.host_memory_begin()
    try:
        assert lib.host_memory_aac_capacity(4608) == 1152
        assert lib.host_memory_aac_output_capacity() == 4608
        assert lib.host_memory_aac_capacity(4609) == -1
        assert lib.host_memory_aac_output_capacity() == 4608
    finally:
        assert lib.host_memory_finish()


def before_control(lib, expected):
    result = []
    for profile in (0, 1, 2):
        assert lib.host_memory_configure(profile)
        assert not lib.host_memory_begin(), 'sealed 06r1 did not reproduce fragmentation failure'
        trace = allocations(lib)
        subband = stat(lib, 15)
        failures = [item for item in trace if item['requested'] == subband and not item['success']]
        assert failures, ('failure was not the real Helix Subband workspace', trace)
        assert any(item['requested'] == 8192 and item['success'] for item in trace)
        assert trace[0]['requested'] == stat(lib, 14) > subband
        assert failures[0]['free_before'] > subband > failures[0]['largest_before']
        assert lib.host_memory_clean()
        result.append({'profile': profile, 'free_holes': HOLES[profile],
                       'initial_free_bytes': stat(lib, 0), 'initial_largest': stat(lib, 1),
                       'decode_state_bytes': stat(lib, 14), 'failed_subband': failures[0],
                       'cleanup_restored_heap': True})
    # The same old source must still decode on a sufficiently contiguous heap;
    # otherwise an ABI or fixture defect could masquerade as fragmentation.
    audio, _, _ = session(lib, 3)
    assert audio == expected
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-source', type=Path, help='Optional sealed 06r1 production source')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temporary:
        lib = compile_memory(Path(temporary))
        try:
            expected, healthy, _ = session(lib, 3)
            profiles = []
            normal_attempts = None
            for profile in (0, 1, 2, 4):
                audio, snapshot, trace = session(lib, profile, 97 if profile == 1 else 480)
                assert audio == expected
                requested = [item['requested'] for item in trace]
                for size in (8192, 4608, 4096, 2306):
                    assert size in requested, ('production capacity changed', size, requested)
                assert snapshot['decode_state_bytes'] < 1024
                assert snapshot['max_requested_allocation'] >= snapshot['subband_bytes']
                profiles.append({'profile': profile, 'free_holes': HOLES[profile], **snapshot})
                if profile == 0:
                    normal_attempts = snapshot['allocation_attempts']
            peak = check_cycles(lib, expected)
            injected = check_failure_injection(lib, expected, normal_attempts)
            check_aac_capacity(lib)
        finally:
            unload_library(lib._handle)
    before = None
    if args.before_source:
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temporary:
            lib = compile_memory(Path(temporary), args.before_source)
            try:
                before = {'source_sha256': hashlib.sha256(args.before_source.read_bytes()).hexdigest(),
                          'source_bytes_unchanged': True, 'profiles': before_control(lib, expected),
                          'healthy_heap_exact_pcm': True}
            finally:
                unload_library(lib._handle)
    report = {'result': 'PASS', 'evidence': 'bounded_first_fit_production_pipeline',
              'device_acceptance': False, 'hardware_heap_dump': False,
              'allocator_header_bytes': 16, 'alignment_bytes': 16,
              'persistent_separators': '32-byte live payload between initially free holes',
              'task_stack_bytes': 8192, 'tcb_and_network_allocations_not_simulated': True,
              'samples_per_cycle': len(expected), 'cycles': 50, 'exact_pcm': True,
              'peak_pipeline_reserved_bytes': peak, 'profiles': profiles, 'healthy': healthy,
              'failure_injections': injected, 'aac_capacity_stub': {'capacity_bytes': 4608,
                  'decoded_4608_accepted': True, 'decoded_4609_rejected': True, 'real_aac_test': False},
              'before_control': before}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: bounded coalescing first-fit heap; real stack/Helix/frame/ring allocations; '
          f'{len(expected)} exact PCM samples x50; {len(injected)} allocation failures/cleanup/reuse; '
          '4 fragmented profiles, AAC 4608/4609 capacity guard (host only)')


if __name__ == '__main__':
    main()
