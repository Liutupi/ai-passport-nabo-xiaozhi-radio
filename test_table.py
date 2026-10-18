"""Host tests for NABO TABLE: the C model must play exactly like the browser prototype.

tests/table_host/golden/*.json were recorded from nabo-table/prototype with
tests/table_host/record_golden.js: a config, a seed, every key press and frame advance,
and the prototype's state after each step. Each test replays one trace on the C model.
"""
import ctypes
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from host_tools import CC, SHARED_EXPORT

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
HOST = ROOT / 'tests/table_host'
SOURCES = [BOARD / f for f in ('nabo_table_model.c', 'nabo_table_bid.c', 'nabo_table_sprites.c', 'nabo_table_text.c',
                                 'nabo_table_gfx.c', 'nabo_table_shapes.c', 'nabo_table_scene.c',
                                 'nabo_table_scene_play.c', 'nabo_dice_render.c', 'nabo_table_font_12.c',
                                 'nabo_menu_font_16.c', 'nabo_ui_font_18.c', 'nabo_table_font_24.c',
                                 'nabo_mono_font_12.c', 'nabo_poster_font_28.c')]
SOURCES += [HOST / 'table_host.c', HOST / 'render_host.c']
PACK = BOARD / 'table_assets/table_sprites.bin'
KEYS = {'up': 0, 'down': 1, 'ok': 2}
KEY_OK_LONG = 3
CUT_S, SLAM_S = 17 / 12, 15 / 12   # the prototype's sprite lengths for the cut and the slam
EV_MOVE, EV_OK, EV_SETTINGS = 1, 2, 1 << 11

temp = tempfile.TemporaryDirectory()
library = Path(temp.name) / 'table.so'
subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT, '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(HOST), '-I', str(BOARD)] + [str(s) for s in SOURCES]
               + ['-lm', '-o', str(library)], check=True)
lib = ctypes.CDLL(str(library))
lib.host_init.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                          ctypes.c_float, ctypes.c_float]
lib.host_key.argtypes = [ctypes.c_int]
lib.host_frames.argtypes = [ctypes.c_int]
lib.host_events.restype = ctypes.c_uint
lib.host_snap.argtypes = [ctypes.c_char_p, ctypes.c_int]
lib.host_render_init.argtypes = [ctypes.c_char_p]
lib.host_draw.argtypes = [ctypes.c_int]
SPRITE_BUFFER = lib.host_render_init(str(PACK).encode())


def snap():
    buf = ctypes.create_string_buffer(8192)
    n = lib.host_snap(buf, len(buf))
    assert n < len(buf), 'snapshot truncated'
    return json.loads(buf.value)


def start(cfg, seed):
    lib.host_init(seed, cfg['target'], cfg['dicePer'], cfg['minOpen'], int(cfg['wild']), CUT_S, SLAM_S)


def step(s):
    if s['a'] == 'k':
        lib.host_key(KEYS[s['k']])
    elif s['a'] == 'f':
        lib.host_frames(s['n'])
    else:
        lib.host_key(KEY_OK_LONG)


def same(want, got):
    """Compare snapshots; die angles are floats, so allow one thousandth of a degree."""
    if 'dc' in want and 'dc' in got:
        wy, gy = want['dc'].pop('y'), got['dc'].pop('y')
        if any(abs(a - b) > 1 for wa, ga in zip(wy, gy) for a, b in zip(wa, ga)):
            return False
    return want == got


class Replay(unittest.TestCase):
    def replay(self, name):
        trace = json.loads((HOST / 'golden' / name).read_text())
        start(trace['cfg'], trace['seed'])
        for i, (s, want) in enumerate(zip(trace['steps'], trace['snaps'])):
            step(s)
            got = snap()
            if not same(dict(want), got):
                self.fail(f'{name}: step {i} {s} differs\n want {want}\n  got {got}')

    def test_traces(self):
        names = sorted(p.name for p in (HOST / 'golden').glob('t*.json'))
        self.assertGreaterEqual(len(names), 6)
        for name in names:
            with self.subTest(trace=name):
                self.replay(name)


class Render(unittest.TestCase):
    def test_dirty_rectangles_match_full_redraws(self):
        # Replay a whole trace one frame at a time; every incremental frame must equal a full redraw.
        trace = json.loads((HOST / 'golden' / 't1_default.json').read_text())
        start(trace['cfg'], trace['seed'])
        frames = 0
        for s in trace['steps']:
            n = s['n'] if s['a'] == 'f' else 1
            if s['a'] != 'f':
                step(s)
            for _ in range(n):
                lib.host_frames(1) if s['a'] == 'f' else None
                self.assertGreaterEqual(lib.host_draw(1), 0, f'frame {frames}: dirty redraw differs')
                frames += 1
        self.assertGreater(frames, 1000)

    def test_ram_budget(self):
        self.assertGreater(SPRITE_BUFFER, 0, 'sprite pack failed to load')
        self.assertLessEqual(SPRITE_BUFFER, 6 * 1024)   # one small-NABO index buffer


class Rules(unittest.TestCase):
    def setUp(self):
        start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True}, 42)

    def test_target_cycles_and_asks_to_persist(self):
        lib.host_key(KEYS['up'])            # menu wraps to the target item
        lib.host_events()
        lib.host_key(KEYS['ok'])
        self.assertEqual(snap()['target'], 5)
        self.assertTrue(lib.host_events() & EV_SETTINGS)

    def test_pause_only_in_play(self):
        lib.host_key(KEY_OK_LONG)
        self.assertEqual(snap()['pause'], -1)      # not on the cover
        lib.host_key(KEYS['ok'])                   # start 21 points
        lib.host_key(KEY_OK_LONG)
        self.assertEqual(snap()['pause'], 0)
        lib.host_frames(600)                       # paused: the deal does not advance
        self.assertEqual(snap()['bj']['d'], 0)

    def test_first_bid_never_offers_open(self):
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                   # liar's dice
        lib.host_frames(200)
        lib.host_key(KEYS['ok'])                   # take the hand-off
        s = snap()['dc']
        self.assertNotIn(0, s['q'])
        self.assertEqual(s['q'][0], 2)             # smallest bid uses min_open

    def test_ones_are_wild_until_someone_bids_ones(self):
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                   # liar's dice, 5 dice each
        for p, row in enumerate([[1, 1, 3, 4, 5], [1, 2, 2, 6, 6]]):
            for i, v in enumerate(row):
                lib.host_set_dice(p, i, v)
        self.assertEqual(lib.host_count(2), 5)     # two 2s plus three wild 1s
        self.assertEqual(lib.host_count(1), 3)     # ones only count themselves
        lib.host_add_bid(0, 3, 1)                  # a bid on ones: zhai, ones stop being wild
        self.assertEqual(lib.host_count(2), 2)
        self.assertEqual(lib.host_count(1), 3)


if __name__ == '__main__':
    unittest.main()
