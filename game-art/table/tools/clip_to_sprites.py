"""Turn an image-to-video clip of NABO into game sprite frames.

Pipeline per clip: decode with ffmpeg -> key out the flat green background and
the drawn table line -> scale so the cap-top-to-table-line height matches the
target size -> place the table line on the anchor row -> crop to the alpha
bounding box. Writes per-size PNG frames and a size report (64-colour palette,
16-level alpha, deflate) so frame counts can be checked against the flash
budget before anything goes into firmware.

Usage:
    python3 tools/clip_to_sprites.py tools/clips.json
"""

from __future__ import annotations

import json
import logging
import subprocess
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)

ROOT = Path(__file__).resolve().parent.parent
SIZES: Dict[str, int] = {"large": 150, "small": 72}   # NABO height above the table, px
CANVAS = 256
ANCHOR: Tuple[int, int] = (128, 224)                  # table-line midpoint in the canvas
PALETTE_COLORS = 64


@dataclass(frozen=True)
class Clip:
    """One animation cut from a source video."""

    name: str
    source: str
    start: float
    end: float
    fps: float
    loop: bool
    table_y: int            # table line row in the source
    cap_top_y: int          # first row of the cap in the source
    events: Dict[str, int] = field(default_factory=dict)


def probe(path: Path) -> Tuple[int, int, float]:
    """Return width, height and frame rate of a video."""
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height,r_frame_rate", "-of", "json", str(path)],
        check=True, capture_output=True, text=True).stdout
    s = json.loads(out)["streams"][0]
    num, den = s["r_frame_rate"].split("/")
    return int(s["width"]), int(s["height"]), float(num) / float(den)


def decode(clip: Clip) -> List[np.ndarray]:
    """Decode the clip's time range, resampled to the clip's fps. A still image gives one frame."""
    path = ROOT / clip.source
    if path.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp"):
        return [np.asarray(Image.open(path).convert("RGB"))]
    w, h, _ = probe(path)
    cmd = ["ffmpeg", "-v", "error", "-ss", f"{clip.start:.3f}", "-to", f"{clip.end:.3f}", "-i", str(path),
           "-vf", f"fps={clip.fps}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    n = len(raw) // (w * h * 3)
    return [np.frombuffer(raw, np.uint8, w * h * 3, i * w * h * 3).reshape(h, w, 3) for i in range(n)]


def key(frame: np.ndarray, table_y: int) -> np.ndarray:
    """Return RGBA with the flat background and the table line made transparent."""
    rgb = frame.astype(np.float32)
    border = np.concatenate([rgb[:8].reshape(-1, 3), rgb[:, :8].reshape(-1, 3), rgb[:, -8:].reshape(-1, 3)])
    bg = np.median(border, axis=0)
    dist = np.abs(rgb - bg).sum(axis=2)
    alpha = np.clip((dist - 28.0) / 40.0, 0.0, 1.0)
    # The drawn table line: dark teal rows around table_y, only where the pixel is line-coloured.
    band = slice(max(0, table_y - 3), table_y + 3)
    line = rgb[band]
    is_line = (line[..., 1] > line[..., 0] + 8) & (line.sum(axis=2) < 200)
    alpha[band][is_line] = 0.0
    # Despill: pull green down to the brighter of red/blue on soft edges.
    out = rgb.copy()
    g_cap = np.maximum(out[..., 0], out[..., 2]) + 12
    spill = (alpha < 1.0) & (out[..., 1] > g_cap)
    out[..., 1][spill] = g_cap[spill]
    rgba = np.dstack([out, alpha * 255.0]).clip(0, 255).astype(np.uint8)
    return rgba


def place(rgba: np.ndarray, clip: Clip, height: int) -> Image.Image:
    """Scale and position one keyed frame on the 256x256 canvas."""
    scale = height / float(clip.table_y - clip.cap_top_y)
    h, w = rgba.shape[:2]
    im = Image.fromarray(rgba, "RGBA")
    # Premultiply before resampling so keyed edges do not pick up background colour.
    pre = np.asarray(im, np.float32)
    pre[..., :3] *= pre[..., 3:4] / 255.0
    small = Image.fromarray(pre.astype(np.uint8), "RGBA").resize((round(w * scale), round(h * scale)), Image.LANCZOS)
    arr = np.asarray(small, np.float32)
    a = arr[..., 3:4]
    arr[..., :3] = np.where(a > 0, arr[..., :3] * 255.0 / np.maximum(a, 1), 0)
    small = Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGBA")
    canvas = Image.new("RGBA", (CANVAS, CANVAS))
    ox = ANCHOR[0] - round(w * scale / 2)
    oy = ANCHOR[1] - round(clip.table_y * scale)
    canvas.paste(small, (ox, oy), small)
    return canvas


def packed_size(im: Image.Image) -> int:
    """Bytes for one frame stored as 64-colour indices + 16-level alpha, deflated, cropped to its bbox."""
    box = im.getbbox()
    if not box:
        return 0
    crop = im.crop(box)
    idx = crop.convert("RGB").quantize(PALETTE_COLORS, method=Image.MEDIANCUT)
    alpha = (np.asarray(crop)[..., 3] >> 4).astype(np.uint8)
    payload = np.asarray(idx, np.uint8).tobytes() + alpha.tobytes()
    return len(zlib.compress(payload, 9)) + PALETTE_COLORS * 2


def run(config_path: Path) -> Dict[str, Dict]:
    """Process every clip listed in the config and write frames plus a report."""
    cfg = json.loads(config_path.read_text())
    out_root = ROOT / cfg["out_dir"]
    report: Dict[str, Dict] = {}
    for c in cfg["clips"]:
        clip = Clip(**c)
        try:
            frames = decode(clip)
        except subprocess.CalledProcessError as e:
            logger.error("decode failed for %s: %s", clip.name, e.stderr)
            raise
        keyed = [key(f, clip.table_y) for f in frames]
        entry = {"fps": clip.fps, "loop": clip.loop, "frames": len(keyed), "events": clip.events, "sizes": {}}
        for size_name, height in SIZES.items():
            d = out_root / size_name / clip.name
            d.mkdir(parents=True, exist_ok=True)
            total = 0
            for i, k in enumerate(keyed):
                im = place(k, clip, height)
                im.save(d / f"{i:02d}.png")
                total += packed_size(im)
            entry["sizes"][size_name] = {"bytes": total, "per_frame": total // max(1, len(keyed))}
        report[clip.name] = entry
        logger.info("%s: %d frames, large %.1f KB, small %.1f KB", clip.name, len(keyed),
                    entry["sizes"]["large"]["bytes"] / 1024, entry["sizes"]["small"]["bytes"] / 1024)
    (out_root / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False))
    return report


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    run(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "tools" / "clips.json")
