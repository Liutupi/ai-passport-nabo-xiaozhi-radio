"""Pack NABO table sprites into table_sprites.bin for the assets partition.

Input: game-art/table/sprites/<large|small>/<anim>/NN.png (256x256 RGBA, table
line at y=224), produced by game-art/table/tools/clip_to_sprites.py.

File layout (little endian):
    header   'NTS1', u16 anim_count, u16 frame_count, u32 anims_off, u32 frames_off
    anims    anim_count x 32 bytes:
             char name[12]; u8 size (0 large, 1 small); u8 frames; u8 fps;
             u8 flags (bit0 loop, bit1 every frame is a keyframe);
             u16 w, h; i16 ox, oy (top-left relative to the table-line anchor);
             u16 first_frame; u16 colors; u32 palette_off (colors x RGB888, index 1..colors)
    frames   frame_count x 8 bytes: u32 rows_off, u32 rle_off
    rows     (h + 1) x u16 per frame, byte offsets into that frame's row data
    frame 0 of each animation is a keyframe, every later frame a delta on the one before:
      keyframe row: tokens, bit7 = repeat. repeat: run = (t & 127) + 2, then one index;
                    literal: run = (t & 127) + 1, then run indices. Index 0 is transparent.
                    (Same scheme as muse_pixel.c.)
      delta row:    bit7 = skip: (t & 127) + 1 pixels unchanged;
                    else literal: t + 1 new indices follow.
    The game keeps one w x h index buffer per playing animation, decodes the keyframe into
    it, then applies deltas in order; a loop restarts from the keyframe.
    Large animations (cover, hand-off and end screens) store every frame as a keyframe, so
    they draw straight from flash with no buffer: RAM is the scarce resource while playing.

Pixels whose colour barely moves between frames keep the previous index (STABLE), which
removes video flicker and makes the deltas small.

Usage:
    python3 tools/generate_table_sprites.py [--check]
"""

from __future__ import annotations

import argparse
import logging
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
from PIL import Image, ImageFilter

logger = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parent.parent
SPRITES = REPO / "game-art" / "table" / "sprites"
BOARD = REPO / "xiaozhi-esp32" / "main" / "boards" / "folotoy" / "ai-passport-nabo"
OUT_BIN = BOARD / "table_assets" / "table_sprites.bin"
OUT_H = BOARD / "nabo_table_sprite_ids.h"
ANCHOR = (128, 224)
EDGE_BG = (13, 40, 36)          # edges are pre-blended over this dark felt/wall tone
ALPHA_CUT = 64                  # below this the pixel is transparent
SIZES = {"large": 0, "small": 1}
COLORS = 64                     # palette size per animation
MODE_FILTER = 0                 # 0 = off (it erased the glasses at 72 px)
MAX_COLOR_ERR = 8.0             # mean per-channel error allowed after decoding
STABLE = 24                     # keep the previous index when the RGB change is below this (sum of channels)


@dataclass(frozen=True)
class AnimSpec:
    """Which source frames go into the pack, and at which size."""

    name: str
    size: str
    step: int = 1               # keep every Nth source frame (source is 12 fps)
    loop: bool = True


# Large frames: cover, handoff and end screens. Small frames: the dealer strip in play.
SPECS: List[AnimSpec] = [
    AnimSpec("spread", "large", 3), AnimSpec("shake", "large", 2), AnimSpec("idle_empty", "large", 3),
    AnimSpec("cover_in", "large", 1, False), AnimSpec("cover", "large", 2), AnimSpec("trophy", "large", 2),
    AnimSpec("cut", "small", 1, False), AnimSpec("idle_cards", "small", 3), AnimSpec("idle_empty", "small", 3),
    AnimSpec("deal", "small", 1, False), AnimSpec("shake", "small", 2), AnimSpec("slam", "small", 1, False),
    AnimSpec("open", "small", 1, False), AnimSpec("point_l", "small", 1, False),
    AnimSpec("point_r", "small", 1, False), AnimSpec("surprise", "small", 1, False),
]


def load_frames(spec: AnimSpec) -> List[np.ndarray]:
    """Load the selected RGBA frames of one animation."""
    d = SPRITES / spec.size / spec.name
    paths = sorted(d.glob("[0-9][0-9].png"))
    if not paths:
        raise FileNotFoundError(f"no frames in {d}")
    return [np.asarray(Image.open(p).convert("RGBA")) for p in paths[::spec.step]]


def flatten(frames: List[np.ndarray]) -> Tuple[List[np.ndarray], List[np.ndarray], Tuple[int, int, int, int]]:
    """Pre-blend edges, cut transparency, crop all frames to their union box."""
    rgbs, masks = [], []
    for f in frames:
        a = f[..., 3:4].astype(np.float32) / 255.0
        rgb = f[..., :3].astype(np.float32) * a + np.array(EDGE_BG, np.float32) * (1 - a)
        rgbs.append(rgb.round().clip(0, 255).astype(np.uint8))
        masks.append(f[..., 3] >= ALPHA_CUT)
    union = np.any(masks, axis=0)
    ys, xs = np.where(union)
    box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    x0, y0, x1, y1 = box
    return [r[y0:y1, x0:x1] for r in rgbs], [m[y0:y1, x0:x1] for m in masks], box


def quantize(rgbs: List[np.ndarray], masks: List[np.ndarray]) -> Tuple[np.ndarray, List[np.ndarray]]:
    """One palette (<= 255 colours) shared by every frame of the animation."""
    strip = Image.fromarray(np.concatenate(rgbs, axis=1), "RGB")
    q = strip.quantize(colors=COLORS, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
    # The art is flat cel shading; the video adds speckle that breaks RLE runs. A small
    # mode filter on the index image removes the speckle without blurring edges.
    pal = np.array(q.getpalette()[: 255 * 3], np.uint8).reshape(-1, 3)
    raw = np.asarray(q, np.uint8)
    if MODE_FILTER:
        # Filter the palette indices themselves (as an 8-bit image), not their colours.
        raw = np.asarray(Image.fromarray(raw, "L").filter(ImageFilter.ModeFilter(MODE_FILTER)))
    idx = raw.astype(np.int32) + 1
    w = rgbs[0].shape[1]
    out = []
    for i, m in enumerate(masks):
        part = idx[:, i * w:(i + 1) * w].copy()
        part[~m] = 0
        out.append(part.astype(np.uint8))
    for i in range(1, len(out)):
        prev, cur = out[i - 1], out[i]
        both = (prev > 0) & (cur > 0)
        drift = np.abs(rgbs[i].astype(np.int32) - pal[np.maximum(prev, 1) - 1].astype(np.int32)).sum(axis=2)
        keep = both & (drift < STABLE)
        cur[keep] = prev[keep]
    used = int(max(p.max() for p in out))
    return pal[:used], out


def encode_row(row: np.ndarray) -> bytes:
    """RLE one row with the muse_pixel token scheme."""
    out = bytearray()
    i, n = 0, len(row)
    while i < n:
        j = i
        while j + 1 < n and row[j + 1] == row[i] and j - i < 128:
            j += 1
        if j - i >= 1:                                   # run of 2..129 equal indices
            out += bytes((0x80 | (j - i - 1), int(row[i])))
            i = j + 1
            continue
        k = i
        while k + 1 < n and k - i < 127 and row[k + 1] != row[k]:
            k += 1
        if k + 1 < n and row[k + 1] == row[k] and k > i:
            k -= 1                                       # leave the start of the next run alone
        out += bytes((k - i,)) + bytes(int(v) for v in row[i:k + 1])
        i = k + 1
    return bytes(out)


def encode_delta_row(prev: np.ndarray, cur: np.ndarray) -> bytes:
    """Skip runs over unchanged pixels, literal runs of new indices."""
    out = bytearray()
    i, n = 0, len(cur)
    while i < n:
        same = cur[i] == prev[i]
        j = i
        while j < n and (cur[j] == prev[j]) == same and j - i < 128:
            j += 1
        if same:
            out.append(0x80 | (j - i - 1))
        else:
            out.append(j - i - 1)
            out += bytes(int(v) for v in cur[i:j])
        i = j
    return bytes(out)


def apply_delta_row(data: bytes, row: np.ndarray) -> None:
    """Reference delta decoder: updates row in place."""
    x, pos = 0, 0
    while pos < len(data):
        t = data[pos]; pos += 1
        run = (t & 127) + 1
        if not t & 0x80:
            row[x:x + run] = np.frombuffer(data[pos:pos + run], np.uint8); pos += run
        x += run


def decode_row(data: bytes, w: int) -> np.ndarray:
    """Reference decoder, the same loop the firmware runs."""
    row, x, pos = np.zeros(w, np.uint8), 0, 0
    while pos < len(data):
        t = data[pos]; pos += 1
        if t & 0x80:
            run = (t & 127) + 2; row[x:x + run] = data[pos]; pos += 1
        else:
            run = (t & 127) + 1; row[x:x + run] = np.frombuffer(data[pos:pos + run], np.uint8); pos += run
        x += run
    return row


def build() -> Tuple[bytes, List[Dict]]:
    """Encode every animation and lay out the file."""
    anims, frames_blob = [], []
    for spec in SPECS:
        rgbs, masks, (x0, y0, x1, y1) = flatten(load_frames(spec))
        pal, idx = quantize(rgbs, masks)
        enc = []
        for k, f in enumerate(idx):
            key = k == 0 or spec.size == "large"
            rows = [encode_row(r) for r in f] if key else [encode_delta_row(p, r) for p, r in zip(idx[k - 1], f)]
            offs, acc = [], 0
            for r in rows:
                offs.append(acc); acc += len(r)
            offs.append(acc)
            if acc > 0xFFFF:
                raise ValueError(f"{spec.name}: frame {k} row data exceeds 64 KB")
            enc.append((struct.pack(f"<{len(offs)}H", *offs), b"".join(rows)))
        anims.append(dict(spec=spec, w=x1 - x0, h=y1 - y0, ox=x0 - ANCHOR[0], oy=y0 - ANCHOR[1],
                          pal=pal, enc=enc, idx=idx, rgb=rgbs, fps=max(1, round(12 / spec.step))))
    n_frames = sum(len(a["enc"]) for a in anims)
    anims_off = 16
    frames_off = anims_off + 32 * len(anims)
    cursor = frames_off + 8 * n_frames
    body, frame_table, anim_table, first = bytearray(), bytearray(), bytearray(), 0
    for a in anims:
        pal_off = cursor + len(body)
        body += a["pal"].tobytes()
        for rows, rle in a["enc"]:
            rows_off = cursor + len(body); body += rows
            rle_off = cursor + len(body); body += rle
            frame_table += struct.pack("<II", rows_off, rle_off)
        s = a["spec"]
        anim_table += struct.pack("<12sBBBBHHhhHHI", s.name.encode(), SIZES[s.size], len(a["enc"]), a["fps"],
                                  (1 if s.loop else 0) | (2 if s.size == "large" else 0), a["w"], a["h"], a["ox"], a["oy"], first, len(a["pal"]), pal_off)
        a["first"] = first
        first += len(a["enc"])
    header = struct.pack("<4sHHII", b"NTS1", len(anims), n_frames, anims_off, frames_off)
    return bytes(header + anim_table + frame_table + body), anims


def check(blob: bytes, anims: List[Dict]) -> None:
    """Decode every frame back from the bytes and compare with the source indices."""
    count, n_frames, anims_off, frames_off = struct.unpack_from("<4sHHII", blob)[1:]
    for k, a in enumerate(anims):
        f0 = struct.unpack_from("<12sBBBBHHhhHHI", blob, anims_off + 32 * k)[9]
        buf = np.zeros((a["h"], a["w"]), np.uint8)
        for i, want in enumerate(a["idx"]):
            rows_off, rle_off = struct.unpack_from("<II", blob, frames_off + 8 * (f0 + i))
            for y in range(a["h"]):
                s, e = struct.unpack_from("<HH", blob, rows_off + 2 * y)
                data = blob[rle_off + s:rle_off + e]
                if i == 0 or a["spec"].size == "large":
                    buf[y] = decode_row(data, a["w"])
                else:
                    apply_delta_row(data, buf[y])
            if not np.array_equal(buf, want):
                raise ValueError(f"{a['spec'].name} frame {i} mismatch")
            # Colour check against the source frame, so a palette bug cannot slip through.
            m = buf > 0
            err = np.abs(a["pal"][buf[m] - 1].astype(np.int32) - a["rgb"][i][m].astype(np.int32)).mean()
            if err > MAX_COLOR_ERR:
                raise ValueError(f"{a['spec'].name} frame {i}: colour error {err:.1f} > {MAX_COLOR_ERR}")
    logger.info("round trip ok: %d animations, %d frames", count, n_frames)


def write_header(anims: List[Dict]) -> None:
    """C header with an index per animation, so the game refers to them by name."""
    lines = ["#pragma once", "/* Generated by tools/generate_table_sprites.py. Do not edit. */", "",
             "#define NABO_TABLE_SPRITES_FILE \"table_sprites.bin\"", "", "typedef enum {"]
    for i, a in enumerate(anims):
        s = a["spec"]
        lines.append(f"    NTS_{s.size.upper()}_{s.name.upper()} = {i},   /* {len(a['enc'])} frames @ {a['fps']} fps, {a['w']}x{a['h']} */")
    lines += [f"    NTS_COUNT = {len(anims)}", "} nabo_table_sprite_t;", ""]
    OUT_H.write_text("\n".join(lines))


def main() -> None:
    """Build the pack, verify it and report sizes."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="only build and verify, do not write files")
    args = ap.parse_args()
    blob, anims = build()
    check(blob, anims)
    for a in anims:
        s = a["spec"]
        size = sum(len(r) + len(e) for r, e in a["enc"]) + len(a["pal"]) * 3
        logger.info("%-6s %-11s %3d frames %3dx%-3d %3d colors %7.1f KB", s.size, s.name, len(a["enc"]),
                    a["w"], a["h"], len(a["pal"]), size / 1024)
    logger.info("total %.1f KB", len(blob) / 1024)
    if args.check:
        return
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(blob)
    write_header(anims)
    logger.info("wrote %s and %s", OUT_BIN.relative_to(REPO), OUT_H.relative_to(REPO))


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    main()
