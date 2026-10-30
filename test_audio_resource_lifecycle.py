"""Compile actual AudioService lifecycle methods against bounded host boundaries.

Production methods/macros are copied verbatim into a temporary translation unit;
the complete production class declaration is retained with only #include lines
removed. FixedQueue is the actual production header. Codec allocations and task
scheduling are explicit stubs: this is not real Opus OOM, microphone/upload/TTS,
or device acceptance. In particular the reported vendor OOM 8-byte suspected
leak is neither reproduced nor declared repaired by these tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from host_tools import CXX

ROOT = Path(__file__).resolve().parent
AUDIO = ROOT / 'xiaozhi-esp32/main/audio'
HOST = ROOT / 'tests/audio_host'
METHODS = ('AudioService', '~AudioService', 'Initialize', 'EnsureCodecsLocked',
           'CloseCodecsLocked', 'ReleaseStoppedCodecs', 'Start', 'Stop', 'WaitStopped',
           'SetDecodeSampleRate', 'IsPlaybackDrainedLocked', 'MarkPlaybackDrainedLocked',
           'PushTaskToEncodeQueue')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def closing_brace(source, first):
    """C++ brace scanner, excluding comments and string/character literals."""
    depth, at = 1, first + 1
    state = None
    while at < len(source):
        char, pair = source[at], source[at:at + 2]
        if state == '//':
            if char == '\n': state = None
        elif state == '/*':
            if pair == '*/': state = None; at += 1
        elif state in ('"', "'"):
            if char == '\\': at += 1
            elif char == state: state = None
        elif pair in ('//', '/*'):
            state = pair; at += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if not depth: return at + 1
        at += 1
    raise AssertionError('unterminated production method')


def excerpt(source, name):
    matches = list(re.finditer(r'(?m)^[^\n;{}]*\bAudioService::' + re.escape(name)
                              + r'\s*\([^;\n]*\)\s*(?:const\s*)?\{', source))
    assert len(matches) == 1, (name, 'method excerpt must be unique', len(matches))
    match = matches[0]
    end = closing_brace(source, match.end() - 1)
    text = source[match.start():end]
    assert source.count(text) == 1 and source[match.start():end] == text
    return text, {'start_line': source.count('\n', 0, match.start()) + 1,
                  'bytes': len(text.encode()), 'sha256': digest(text.encode())}


def generate(directory):
    source_bytes = (AUDIO / 'audio_service.cc').read_bytes()
    header_bytes = (AUDIO / 'audio_service.h').read_bytes()
    source, header = source_bytes.decode(), header_bytes.decode()
    declarations = ''.join(line for line in header.splitlines(keepends=True)
                           if not re.match(r'^\s*#\s*include\b', line))
    # No field/type/signature changes, substitution, or method reimplementation.
    (directory / 'production_audio_service.h').write_bytes(declarations.encode())
    first = source.index('#define RATE_CVT_CFG')
    last = source.index('#if CONFIG_IDF_TARGET_', first)
    macros = source[first:last]
    extracted, provenance = [], {}
    for name in METHODS:
        text, record = excerpt(source, name)
        extracted.append(text)
        provenance[name] = record
    (directory / 'production_audio_methods.inc').write_bytes(
        (macros + '\n#define TAG "AudioServiceHost"\n\n' + '\n\n'.join(extracted) + '\n').encode())
    return {'audio_service_cc_sha256': digest(source_bytes), 'audio_service_h_sha256': digest(header_bytes),
            'fixed_queue_h_sha256': digest((AUDIO / 'fixed_queue.h').read_bytes()),
            'stripped_header_includes_only': True, 'method_excerpts': provenance,
            'production_macros_sha256': digest(macros.encode())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix='nabo-audio-resource-') as temporary:
        directory = Path(temporary)
        provenance = generate(directory)
        for nabo, processor in ((1, 0), (1, 1), (0, 0), (0, 1)):
            executable = directory / f'lifecycle-{nabo}-{processor}.exe'
            subprocess.run([CXX, '-std=gnu++20', '-O1', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-pthread',
                            f'-DCONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO={nabo}',
                            f'-DCONFIG_USE_AUDIO_PROCESSOR={processor}',
                            '-I', str(directory), '-I', str(HOST), '-I', str(AUDIO),
                            str(HOST / 'lifecycle.cc'), '-o', str(executable)],
                           cwd=ROOT, check=True, timeout=90)
            run = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True, timeout=20)
            if run.returncode:
                raise AssertionError(f'actual-method host failed ({nabo=}, {processor=}):\n{run.stdout}\n{run.stderr}')
            print(run.stdout, end='', flush=True)
            records = [line.removeprefix('AUDIO_HOST_JSON ') for line in run.stdout.splitlines()
                       if line.startswith('AUDIO_HOST_JSON ')]
            assert len(records) == 1
            results.append(json.loads(records[0]))
        # A concurrent production edit must not turn this run into stale evidence.
        assert provenance['audio_service_cc_sha256'] == digest((AUDIO / 'audio_service.cc').read_bytes())
        assert provenance['audio_service_h_sha256'] == digest((AUDIO / 'audio_service.h').read_bytes())
    report = {'status': 'PASS', 'method': 'verbatim production methods and full class with include-only boundary',
              'provenance': provenance, 'configurations': results,
              'heap_model': {'allocation_policy': 'bounded first-fit, split and coalesce',
                             'alignment_bytes': 16, 'header_bytes': 16,
                             'synthetic_free_holes_bytes': [32768, 24576, 18432, 12288, 8192, 8192],
                             'profile_is_device_dump': False,
                             'codec_workspaces_are_aggregate_size_stubs': True,
                             'host_observer_cpp_allocations_outside_arena': True},
              'timer_model': {'start_active_returns_invalid_state': True,
                              'stop_inactive_returns_invalid_state': True,
                              'old_task_rearms_between_Stop_and_ack': True,
                              'rearm_restart_cycles_per_configuration': 10,
                              'real_timer_failure_rollback_cases_retained': True},
              'limits': ['RTOS, timer, codec and resampler are explicit stubs; no hardware task body is exercised.',
                         'Vendor Opus internal allocation behavior and suspected 8-byte OOM leak are unverified.',
                         'Microphone recording, upload, TTS, wake/AEC and physical task scheduling require device testing.'],
              'device_or_network_access': False}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: production audio resource lifecycle; four board/processor configurations; no device/network access.')


if __name__ == '__main__':
    main()
