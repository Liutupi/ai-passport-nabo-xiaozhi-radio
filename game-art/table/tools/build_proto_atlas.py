"""Pack sprites/<size>/<anim>/NN.png into prototype/assets/nabo_sprites.js.

Each animation becomes one horizontal strip, cropped to the union of its
frames' alpha boxes, embedded as a data URI (so the page also works from
file:// and as a published artifact). Offsets are relative to the table-line
anchor, so the page draws a frame at (anchor_x + ox, rim_y + oy).

Usage:
    python3 tools/build_proto_atlas.py
"""

from __future__ import annotations

import base64
import io
import json
import logging
from pathlib import Path
from typing import Dict

from PIL import Image

logger = logging.getLogger(__name__)

ROOT = Path(__file__).resolve().parent.parent
SPRITES = ROOT / "sprites"
OUT = ROOT / "prototype" / "assets" / "nabo_sprites.js"
ANCHOR = (128, 224)
# 每个尺寸只打包游戏里真的会用到的动作：大图用在封面、交接屏、整场结束；小图用在对局顶部
USED: Dict[str, set] = {
    "large": {"spread", "shake", "idle_empty", "cover_in", "cover", "trophy"},
    "small": {"cut", "idle_cards", "idle_empty", "deal", "shake", "slam", "open", "point_l", "point_r", "surprise"},
}


def strip(anim_dir: Path) -> Dict:
    """Build one animation strip and its placement data."""
    frames = [Image.open(p).convert("RGBA") for p in sorted(anim_dir.glob("[0-9][0-9].png"))]
    boxes = [f.getbbox() for f in frames if f.getbbox()]
    x0 = min(b[0] for b in boxes); y0 = min(b[1] for b in boxes)
    x1 = max(b[2] for b in boxes); y1 = max(b[3] for b in boxes)
    w, h = x1 - x0, y1 - y0
    sheet = Image.new("RGBA", (w * len(frames), h))
    for i, f in enumerate(frames):
        sheet.paste(f.crop((x0, y0, x1, y1)), (i * w, 0))
    buf = io.BytesIO()
    sheet.save(buf, "WEBP", quality=88, method=6)   # 带透明的有损 WebP，原型够用；固件另有自己的编码
    return {"n": len(frames), "w": w, "h": h, "ox": x0 - ANCHOR[0], "oy": y0 - ANCHOR[1],
            "img": "data:image/webp;base64," + base64.b64encode(buf.getvalue()).decode()}


def main() -> None:
    """Pack every size and animation found under sprites/."""
    report = json.loads((SPRITES / "report.json").read_text())
    atlas: Dict[str, Dict] = {}
    for size_dir in sorted(p for p in SPRITES.iterdir() if p.is_dir()):
        atlas[size_dir.name] = {}
        for anim_dir in sorted(p for p in size_dir.iterdir() if p.is_dir() and p.name in USED.get(size_dir.name, set())):
            entry = strip(anim_dir)
            meta = report.get(anim_dir.name, {})
            entry.update(fps=meta.get("fps", 12), loop=meta.get("loop", True), events=meta.get("events", {}))
            atlas[size_dir.name][anim_dir.name] = entry
            logger.info("%s/%s: %d frames %dx%d", size_dir.name, anim_dir.name, entry["n"], entry["w"], entry["h"])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("// 由 tools/build_proto_atlas.py 生成，不要手改\nwindow.NABO_SPRITES = " + json.dumps(atlas) + ";\n")
    logger.info("wrote %s (%.1f MB)", OUT, OUT.stat().st_size / 1e6)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    main()
