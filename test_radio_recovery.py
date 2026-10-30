"""Production recovery/telemetry and output transitions, with an offline 03 control.

The clock, producer and HTTP boundary are deterministic host substitutes. These
checks measure the recovery policy; they do not establish network/device quality.
"""
import argparse
import ctypes as C
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
spec = importlib.util.spec_from_file_location('fixture', ROOT / 'tests/legacy/test_radio_decoder.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
PAYLOAD = fixture.FIXTURE * 192


class Telemetry(C.Structure):
    _fields_ = [(name, C.c_uint) for name in (
        'ring', 'rebuffers', 'rebuffer_ms', 'reconnects', 'open_ms',
        'heap_before_open', 'largest_before_open', 'stall_reason')]


def compile_engine(directory, baseline=False):
    vendor = BOARD / 'helix'
    sources = [f for f in sorted(vendor.glob('*.c')) if not f.name.startswith('._')]
    sources += [f for f in sorted((vendor / 'real').glob('*.c'))
                if f.name != 'buffers.c' and not f.name.startswith('._')]
    sources += [BOARD / name for name in (
        'nabo_helix_buffers.c', 'nabo_radio_decoder.c', 'nabo_radio_dsp.c', 'nabo_radio_catalog.c')]
    engine = ROOT / 'tests/radio_host/engine.c'
    if baseline:
        engine = directory / 'engine.c'
        engine.write_bytes((ROOT / 'tests/radio_host/engine.c').read_bytes())
        old_source = subprocess.check_output([
            'git', 'show', 'f66616a:xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/nabo_radio.c'],
            cwd=ROOT)
        (directory / 'nabo_radio.c').write_bytes(old_source)
    dll = directory / ('baseline.dll' if baseline else 'recovery.dll')
    subprocess.run([
        CC, '-std=gnu11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-parameter', '-Wno-unused-but-set-variable', SHARED_EXPORT,
        '-DNABO_HELIX_PORTABLE', *(['-DNRF_BASELINE'] if baseline else []),
        '-I', str(BOARD), '-I', str(ROOT / 'tests/radio_host'),
        '-I', str(vendor / 'pub'), '-I', str(vendor / 'real'),
        *map(str, sources), str(engine), '-o', str(dll)], check=True)
    lib = C.CDLL(str(dll))
    for name in ('nabo_radio_begin', 'nabo_radio_finished', 'nabo_radio_due', 'host_clean'):
        getattr(lib, name).restype = C.c_bool
    lib.nabo_radio_read.argtypes = [C.POINTER(C.c_int16), C.c_size_t, C.c_int]
    lib.nabo_radio_read.restype = C.c_size_t
    lib.host_feed.argtypes = [C.c_void_p, C.c_size_t]
    lib.host_feed.restype = C.c_size_t
    if not baseline:
        lib.nabo_radio_telemetry.argtypes = [C.POINTER(Telemetry)]
        lib.nabo_radio_tuning_noise.restype = C.c_bool
        lib.host_open_begin.restype = C.c_int64
        lib.host_open_end.argtypes = [C.c_int64]
        lib.host_audio_smooth.argtypes = [C.POINTER(C.c_int16), C.c_uint, C.c_uint, C.c_uint]
    return lib


def stats(lib):
    result = Telemetry()
    lib.nabo_radio_telemetry(C.byref(result))
    return result


def begin(lib, capacity):
    lib.host_limit_ring(capacity)
    lib.nabo_radio_select_station(0)
    assert lib.nabo_radio_begin()
    assert lib.host_ring_capacity() == capacity


def finish(lib):
    lib.nabo_radio_stop()
    lib.host_eof()
    lib.nabo_radio_finish()
    assert lib.host_clean()


def check_telemetry_and_thresholds(lib):
    source = C.create_string_buffer(PAYLOAD)
    dst = (C.c_int16 * 480)()
    for capacity in (8192, 16384, 32768):
        begin(lib, capacity)
        assert lib.nabo_radio_tuning_noise()
        lib.nabo_radio_set_noise(False)
        assert not lib.nabo_radio_tuning_noise()
        lib.nabo_radio_set_noise(True)
        initial = min(12288, capacity * 3 // 4)
        offset = lib.host_feed(source, initial - 1)
        assert offset == initial - 1
        assert lib.nabo_radio_read(dst, 480, 0) == 0
        lib.host_advance(111)
        first = stats(lib)
        assert (first.ring, first.rebuffers, first.rebuffer_ms, first.reconnects) == (capacity, 0, 0, 0)
        assert lib.nabo_radio_tuning_noise()
        offset += lib.host_feed(C.byref(source, offset), 1)
        assert lib.nabo_radio_read(dst, 480, 0) > 0
        assert not lib.nabo_radio_tuning_noise()
        for _ in range(1000):
            if not lib.nabo_radio_read(dst, 480, 0):
                break
        else:
            raise AssertionError('did not exhaust the initial cushion')
        assert stats(lib).rebuffers == 1
        assert not lib.nabo_radio_tuning_noise()
        for _ in range(4):
            assert lib.nabo_radio_read(dst, 480, 0) == 0
        assert stats(lib).rebuffers == 1
        before = stats(lib).rebuffer_ms
        lib.host_advance(137)
        assert stats(lib).rebuffer_ms == before + 137
        recovery = min(4096, capacity // 4)
        offset += lib.host_feed(C.byref(source, offset), recovery - 1)
        assert lib.nabo_radio_read(dst, 480, 0) == 0
        offset += lib.host_feed(C.byref(source, offset), 1)
        assert lib.nabo_radio_read(dst, 480, 0) > 0
        ended = stats(lib).rebuffer_ms
        lib.host_advance(55)
        assert stats(lib).rebuffer_ms == ended
        for _ in range(1000):
            if not lib.nabo_radio_read(dst, 480, 0):
                break
        else:
            raise AssertionError('did not exhaust the recovery cushion')
        assert stats(lib).rebuffers == 2
        lib.host_advance(37)
        assert stats(lib).rebuffer_ms == ended + 37
        lib.nabo_radio_stop()
        lib.host_eof()
        lib.nabo_radio_read(dst, 480, 0)  # closes the wait; pause is not another rebuffer
        stopped = stats(lib)
        lib.host_advance(83)
        assert stats(lib).rebuffer_ms == stopped.rebuffer_ms
        assert stats(lib).rebuffers == 2
        finish(lib)

    begin(lib, 8192)
    reset = stats(lib)
    assert (reset.rebuffers, reset.rebuffer_ms, reset.reconnects, reset.open_ms,
            reset.stall_reason) == (0, 0, 0, 0, 0)
    started = lib.host_open_begin()
    opening = stats(lib)
    assert opening.heap_before_open > 0 and opening.largest_before_open > 0
    lib.host_advance(237)
    lib.host_open_end(started)
    assert stats(lib).open_ms == 237
    for reason in range(1, 6):
        lib.host_reconnect(reason)
        current = stats(lib)
        assert current.reconnects == reason and current.stall_reason == reason
    for buffered, kbps, expected in (
            (0, 64, 8000), (8192, 64, 8000), (16384, 64, 8000),
            (32768, 64, 8000), (32768, 128, 8000), (32768, 32, 8192),
            (32768, 0, 8000)):
        assert lib.host_stall_ms(buffered, kbps) == expected
    finish(lib)
    begin(lib, 8192)
    assert stats(lib).reconnects == 0 and stats(lib).rebuffer_ms == 0
    # EOF is allowed to bypass either prefill threshold; it is not starvation.
    assert lib.host_feed(source, len(fixture.FIXTURE)) == len(fixture.FIXTURE)
    lib.host_eof()
    total = 0
    for _ in range(20):
        if lib.nabo_radio_finished():
            break
        total += lib.nabo_radio_read(dst, 480, 0)
    assert total > 0 and lib.nabo_radio_finished()
    assert stats(lib).rebuffers == 0
    finish(lib)


def check_initial_allocation_backoff(lib):
    # Preserve the actual protection policy: the observed 19-20 KiB heap is
    # insufficient for the smallest 8 KiB ring plus the 16 KiB network reserve.
    # A delayed retry must not make that low-memory case silently permissive.
    lib.host_limit_ring(32768)
    lib.host_heap_budget(20000, 3328)
    lib.nabo_radio_select_station(0)
    assert lib.nabo_radio_due()
    assert not lib.nabo_radio_begin()
    assert lib.host_clean()
    delays = [3000, 6000, 12000, 24000, 30000, 30000]
    for delay in delays:
        assert not lib.nabo_radio_due()
        lib.host_advance(delay-1)
        assert not lib.nabo_radio_due()
        assert not lib.nabo_radio_begin(), 'allocation retry ran before its deadline'
        assert lib.host_clean()
        lib.host_advance(1)
        assert lib.nabo_radio_due()
        assert not lib.nabo_radio_begin(), 'low-memory retries relaxed the ring budget'
        assert lib.host_clean()

    # Cancellation prevents retries even after the largest pending interval.
    # A deliberate resume is a new play generation and gets a fresh 3 s delay.
    lib.nabo_radio_stop()
    lib.host_advance(30001)
    assert not lib.nabo_radio_due()
    assert not lib.nabo_radio_begin()
    lib.nabo_radio_toggle_play()
    assert lib.nabo_radio_due(), 'manual resume inherited the allocation deadline'
    assert not lib.nabo_radio_begin()
    lib.host_advance(2999)
    assert not lib.nabo_radio_due()
    lib.host_advance(1)
    assert lib.nabo_radio_due()
    assert not lib.nabo_radio_begin()

    # Selecting another station also resets the timer and the failure sequence,
    # rather than waiting for the previous station's pending 6 s retry.
    lib.nabo_radio_select_station(1)
    assert lib.nabo_radio_due(), 'station change inherited the allocation deadline'
    assert not lib.nabo_radio_begin()
    lib.host_advance(2999)
    assert not lib.nabo_radio_due()
    lib.host_advance(1)
    assert lib.nabo_radio_due()

    # Total bytes alone remain insufficient evidence: metadata overhead and
    # contiguous network blocks still have to meet the original guards.
    for available, largest in ((19628, 3328), (20136, 3328), (24576, 7680), (256*1024, 3328)):
        lib.host_heap_budget(available, largest)
        lib.nabo_radio_select_station(0)
        assert not lib.nabo_radio_begin(), (available, largest)
        assert lib.host_clean()

    # Restore enough memory during a waiting interval. The deadline still
    # applies; when it expires the unchanged guards permit a safe 8 KiB ring.
    lib.host_heap_budget(30500, 7680)
    assert not lib.nabo_radio_due()
    assert not lib.nabo_radio_begin()
    lib.host_advance(2999)
    assert not lib.nabo_radio_due()
    lib.host_advance(1)
    assert lib.nabo_radio_due()
    assert lib.nabo_radio_begin()
    assert lib.host_ring_capacity() == 8192
    # Allocation alone must not reset the backoff. Repeated pipelines that
    # produce no PCM and are released for network-memory pressure keep the
    # failure sequence, rather than spinning at the first 3 s interval.
    source = C.create_string_buffer(PAYLOAD)
    dst = (C.c_int16 * 480)()
    for delay in (6000, 12000):
        lib.host_memory_release()
        assert lib.nabo_radio_read(dst, 480, 0) == 0
        assert lib.nabo_radio_finished()
        lib.nabo_radio_finish()
        assert lib.host_clean()
        lib.host_advance(delay-1)
        assert not lib.nabo_radio_due(), 'no-PCM pipeline reset allocation backoff'
        lib.host_advance(1)
        assert lib.nabo_radio_due()
        assert lib.nabo_radio_begin()
        assert lib.host_ring_capacity() == 8192

    # Only a successful real decoder frame clears the allocation backoff.
    # Drain the produced PCM before owner teardown, matching normal lifecycle.
    assert lib.host_feed(source, 6144) == 6144
    assert lib.nabo_radio_read(dst, 480, 0) > 0
    lib.host_memory_release()
    for _ in range(100):
        if lib.nabo_radio_finished():
            break
        lib.nabo_radio_read(dst, 480, 0)
    assert lib.nabo_radio_finished()
    lib.nabo_radio_finish()
    assert lib.host_clean()
    lib.host_advance(2999)
    assert not lib.nabo_radio_due()
    lib.host_advance(1)
    assert lib.nabo_radio_due(), 'real PCM did not reset allocation backoff'
    finish(lib)
    lib.host_heap_budget(256*1024, 256*1024)
    return {'initial_allocation_retry_delays_ms': delays,
            'low_heap_guard_retained': True, 'cancel_resume_selection_reset': True,
            'no_pcm_rebuild_keeps_backoff': True, 'real_pcm_resets_backoff': True,
            'restored_heap_ring_bytes': 8192}


def check_retry_generation_isolation(lib):
    lib.host_heap_budget(20000, 3328)
    lib.nabo_radio_select_station(0)
    assert not lib.nabo_radio_begin()  # publishes the speaker owner's generation
    assert lib.host_clean()
    lib.host_schedule_retry(30000)
    assert not lib.nabo_radio_due()
    lib.nabo_radio_select_station(1)
    assert lib.nabo_radio_due()
    # Simulate the old owner writing its retry after the UI chose a new station.
    lib.host_schedule_retry(30000)
    assert lib.nabo_radio_due(), 'late old-generation retry delayed the selected station'
    lib.nabo_radio_select_station(2)
    assert lib.host_retry_deadline() == 0
    lib.host_allocation_backoff()
    assert lib.host_retry_deadline() == 0, 'old-generation allocation failure wrote a deadline'
    assert lib.nabo_radio_due()
    assert not lib.nabo_radio_begin()
    lib.host_advance(2999)
    assert not lib.nabo_radio_due()
    lib.host_advance(1)
    assert lib.nabo_radio_due()
    # Manual resume is another new generation and has the same isolation.
    lib.nabo_radio_stop()
    lib.nabo_radio_toggle_play()
    lib.host_schedule_retry(30000)
    assert lib.nabo_radio_due(), 'old-generation retry delayed manual resume'
    lib.nabo_radio_stop()
    lib.host_heap_budget(256*1024, 256*1024)
    assert lib.host_clean()


def check_output_transitions(lib):
    maximum_jump = 0
    for rate in (8000, 16000, 22050, 24000):
        count = rate // 50
        for level in (-32768, -12000, 12000, 32767):
            lib.host_audio_reset()
            full = (C.c_int16 * count)(*([level] * count))
            lib.host_audio_smooth(full, count, count, rate)
            assert list(full) == [level] * count
            tail = []
            for _ in range(3):
                block = (C.c_int16 * count)()
                lib.host_audio_smooth(block, 0, count, rate)
                tail.extend(block)
            assert tail[-1] == 0
            assert all(abs(b) <= abs(a) for a, b in zip([level] + tail, tail))
            assert tail[rate * 40 // 1000 - 1] == 0
            jump = max(abs(b-a) for a, b in zip([level] + tail, tail))
            maximum_jump = max(maximum_jump, jump)
            assert jump <= abs(level) // (rate * 20 // 1000) + 2
            # A short real block reaches zero before padding. Its untouched
            # prefix remains exact, and repeated empty blocks stay silent.
            lib.host_audio_reset()
            decoded = count * 3 // 4
            short = (C.c_int16 * count)(*([level] * decoded + [0] * (count-decoded)))
            lib.host_audio_smooth(short, decoded, count, rate)
            fade = max(1, rate * 5 // 1000)
            assert list(short)[:max(0, decoded-fade)] == [level] * max(0, decoded-fade)
            assert short[decoded-1] == 0 and all(x == 0 for x in short[decoded:])
            empty = (C.c_int16 * count)()
            lib.host_audio_smooth(empty, 0, count, rate)
            assert not any(empty)
            # New PCM may return after only one 20 ms block of the 30 ms
            # synthetic tail. Join that still nonzero level to resumed PCM.
            lib.host_audio_reset()
            full = (C.c_int16 * count)(*([level] * count))
            lib.host_audio_smooth(full, count, count, rate)
            empty = (C.c_int16 * count)()
            lib.host_audio_smooth(empty, 0, count, rate)
            last = empty[-1]
            assert last != 0
            resumed = (C.c_int16 * count)()  # decoded quiet PCM, not missing input
            lib.host_audio_smooth(resumed, count, count, rate)
            fade = max(1, rate * 5 // 1000)
            assert abs(resumed[0]-last) <= abs(last) // fade + 2
            assert max(abs(b-a) for a, b in zip([last] + list(resumed), resumed)) <= abs(last) // fade + 2
            assert all(abs(b) <= abs(a) for a, b in zip([last] + list(resumed), resumed))
            assert all(x == 0 for x in resumed[fade-1:])
    return maximum_jump


def check_automatic_rebuild(lib):
    source = C.create_string_buffer(PAYLOAD)
    dst = (C.c_int16 * 480)()
    for already_waiting, allocation_failure in ((True, False), (True, True), (False, False)):
        begin(lib, 8192)
        assert lib.host_feed(source, 6144) == 6144
        assert lib.nabo_radio_read(dst, 480, 0) > 0
        if not already_waiting:
            lib.host_eof()
        for _ in range(1000):
            if not lib.nabo_radio_read(dst, 480, 0):
                break
        else:
            raise AssertionError('did not exhaust the pre-rebuild stream')
        assert stats(lib).rebuffers == (1 if already_waiting else 0)
        lib.host_reconnect(2)
        lib.host_advance(250)
        prior = stats(lib)
        # The worker may terminate after a failed reconnect. Reclaiming its
        # pipeline must retain this selection's gap and tuning-noise state.
        lib.host_eof()
        lib.nabo_radio_finish()
        assert lib.host_clean()
        assert not lib.nabo_radio_tuning_noise()
        assert stats(lib).rebuffers == 1, 'finish failed to retain/start this selection gap'
        lib.host_advance(1000)
        assert stats(lib).rebuffer_ms == prior.rebuffer_ms + 1000
        if allocation_failure:
            lib.host_limit_ring(0)
            assert not lib.nabo_radio_begin()
            assert lib.host_clean()
            assert stats(lib).rebuffers == 1
            assert stats(lib).reconnects == prior.reconnects
            assert stats(lib).rebuffer_ms == prior.rebuffer_ms + 1000
            lib.host_limit_ring(8192)
            lib.host_advance(3000)
        assert lib.nabo_radio_begin(), 'automatic restart was not due'
        assert not lib.nabo_radio_tuning_noise(), 'automatic rebuild reintroduced tuning hiss'
        assert stats(lib).rebuffers == 1
        assert stats(lib).reconnects == prior.reconnects
        expected_ms = prior.rebuffer_ms + 1000 + (3000 if allocation_failure else 0)
        assert stats(lib).rebuffer_ms == expected_ms
        assert lib.host_feed(source, 2047) == 2047
        assert lib.nabo_radio_read(dst, 480, 0) == 0
        lib.host_advance(17)
        assert lib.host_feed(C.byref(source, 2047), 1) == 1
        assert lib.nabo_radio_read(dst, 480, 0) > 0, 'automatic rebuild used the first-connect threshold'
        recovered = stats(lib)
        assert recovered.rebuffer_ms == expected_ms + 17
        assert recovered.rebuffers == 1
        lib.host_advance(41)
        assert stats(lib).rebuffer_ms == recovered.rebuffer_ms
        finish(lib)


def check_stop_during_retry_gap(lib):
    begin(lib, 8192)
    source = C.create_string_buffer(PAYLOAD)
    dst = (C.c_int16 * 480)()
    assert lib.host_feed(source, 6144) == 6144
    assert lib.nabo_radio_read(dst, 480, 0) > 0
    lib.host_eof()
    lib.nabo_radio_finish()
    assert lib.host_clean()
    assert stats(lib).rebuffers == 1
    lib.host_advance(400)
    before_stop = stats(lib).rebuffer_ms
    assert before_stop >= 400
    lib.nabo_radio_stop()
    lib.host_advance(1)  # the speaker-owner idle tick observes stop and closes the gap
    closed = stats(lib)
    lib.host_advance(5000)
    assert stats(lib).rebuffer_ms == closed.rebuffer_ms
    assert closed.rebuffer_ms >= before_stop
    assert stats(lib).rebuffers == 1
    assert not lib.nabo_radio_begin()
    assert lib.host_clean()


def simulate_hole(lib, hole_ms, candidate):
    begin(lib, 8192)
    source = C.create_string_buffer(PAYLOAD)
    offset = lib.host_feed(source, 6144)
    audio = []
    silence_samples = 0
    eof = False
    for tick in range(2000):
        elapsed = tick * 20
        if not (2000 <= elapsed < 2000 + hole_ms) and offset < len(PAYLOAD):
            offered = min(160, len(PAYLOAD)-offset)  # 64 kbit/s, every 20 ms
            sent = lib.host_feed(C.byref(source, offset), offered)
            assert sent == offered, 'deterministic producer unexpectedly overflowed the ring'
            offset += sent
        if offset == len(PAYLOAD) and not eof:
            lib.host_eof()
            eof = True
        if lib.nabo_radio_finished():
            break
        block = (C.c_int16 * 480)()
        n = lib.nabo_radio_read(block, 480, 0)
        audio.extend(block[:n])
        if elapsed >= 1000 and not lib.nabo_radio_finished():
            silence_samples += 480-n
        if candidate:
            lib.host_audio_smooth(block, n, 480, 24000)
        lib.host_advance(20)
    else:
        raise AssertionError('simulated stream failed to drain')
    assert offset == len(PAYLOAD) and eof
    result = {'hole_ms': hole_ms, 'silent_ms': silence_samples / 24,
              'decoded_samples': len(audio), 'fed_bytes': offset}
    if candidate:
        current = stats(lib)
        result.update(rebuffers=current.rebuffers, rebuffer_ms=current.rebuffer_ms)
        assert current.rebuffers >= 1
    finish(lib)
    return result, audio


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--report', type=Path)
    parser.add_argument('--candidate', default='worktree', help='Label for the source under test')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as temporary:
        directory = Path(temporary)
        control = compile_engine(directory, baseline=True)
        current = compile_engine(directory)
        try:
            check_telemetry_and_thresholds(current)
            allocation = check_initial_allocation_backoff(current)
            check_retry_generation_isolation(current)
            maximum_jump = check_output_transitions(current)
            check_automatic_rebuild(current)
            check_stop_during_retry_gap(current)
            results = []
            for hole_ms in (1000, 3000):
                old, old_audio = simulate_hole(control, hole_ms, candidate=False)
                new, new_audio = simulate_hole(current, hole_ms, candidate=True)
                assert old['decoded_samples'] == new['decoded_samples']
                assert old['fed_bytes'] == new['fed_bytes'] == len(PAYLOAD)
                assert new['silent_ms'] < old['silent_ms'], (old, new)
                changed = [i for i, (a, b) in enumerate(zip(old_audio, new_audio)) if a != b]
                assert len(changed) <= new['rebuffers'] * 120, 'changed PCM outside recovery fades'
                if new['rebuffers'] == 1:
                    assert not changed or max(changed)-min(changed) < 120
                results.append({'candidate03': old, 'candidate_under_test': new})
            report = {'environment': 'host-only deterministic offline MP3; no device/network QA',
                      'candidate_under_test': args.candidate, 'baseline_source': 'f66616a',
                      'max_synthetic_tail_step': maximum_jump,
                      'allocation_retry': allocation,
                      'late_old_generation_retry_isolated': True,
                      'metric_definition': 'silent_ms counts output-clock slots without real decoded PCM, '
                                           'including short-block padding; a 30 ms synthetic tail is not music. '
                                           'This is not final-PCM threshold detection or a device acoustic measurement.',
                      'holes': results}
            if args.report:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('RECOVERY_RESULTS ' + json.dumps(report, ensure_ascii=False))
            print('PASS: production telemetry, initial/recovery thresholds, live duration, selection/play-generation '
                  'reset/EOF, automatic rebuild/allocation failure, 3/6/12/24/30 s initial-allocation backoff '
                  'with unchanged memory guards and cancel/selection reset, 8 s network grace helpers, '
                  '5 ms short fades/quick-resume blend and 30 ms empty tails; '
                  '1 s/3 s holes vs exact candidate03 source with compressed/sample conservation')
        finally:
            unload_library(current._handle)
            unload_library(control._handle)


if __name__ == '__main__':
    main()
