#!/usr/bin/env python3
"""Grok 绿幕视频 -> 抠图、去溢色、缩放到游戏尺寸的 RGBA 帧（numpy 数组，存为 .npz）。

视频 480×480，桌沿上边缘在第 420 行（起始图 1024 中的 896 行）。游戏里 NABO 帽顶到桌沿 124 px，
所以整帧缩放 124/376.5 -> 158×158，只保留桌沿以上 139 行。
用法：python tools/clip_to_frames.py <视频目录> <输出目录> [--fps 12]
"""
import argparse, json, subprocess
from pathlib import Path
import numpy as np
from PIL import Image

SRC = 480
DESK = 420                       # 视频中桌沿上边缘所在行
SCALE = 124 / (802 * SRC / 1024)  # 帽顶到桌沿 802/1024 -> 124 px
OUT_W = round(SRC * SCALE)        # 158
OUT_H = round(DESK * SCALE)       # 139

def frames(path, fps):
    cmd = ["ffmpeg", "-v", "error", "-i", str(path), "-vf", f"fps={fps}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, SRC, SRC, 3)

def key(rgb):
    """绿幕 -> RGBA。绿度 = G - max(R, B)；溢色压回 max(R, B)。"""
    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    green = g - np.maximum(r, b)
    alpha = 1.0 - np.clip((green - 40.0) / (150.0 - 40.0), 0, 1)
    spill = np.maximum(r, b)
    g2 = np.where(green > 0, np.minimum(g, spill + 8), g)
    # 边缘（离透明区 4 个源像素以内）再压一次：运动模糊的指尖和发梢会混进黄绿色
    solid = alpha > 0.98
    near = ~solid
    for _ in range(4):
        near = near | np.roll(near, 1, 0) | np.roll(near, -1, 0) | np.roll(near, 1, 1) | np.roll(near, -1, 1)
    g2 = np.where(near, np.minimum(g2, (r + b) / 2 + 10), g2)
    # NABO 身上没有黄绿色：G 接近 R 又明显高于 R、B 平均的像素是模糊混进的绿幕（挥手指尖）
    yellow_green = (g2 > (r + b) / 2 + 25) & (g2 > r * 0.92)
    g2 = np.where(yellow_green, np.minimum(g2, r * 0.85), g2)
    out = np.stack([r, g2, b, alpha * 255], -1)
    out[DESK:] = 0                       # 桌沿以下由程序绘制
    return out

def shrink(rgba):
    """预乘 alpha 后面积平均缩小，避免绿色边缘渗进颜色。"""
    a = rgba[..., 3:4] / 255.0
    pre = np.concatenate([rgba[..., :3] * a, a * 255], -1).astype(np.float32)
    im = Image.fromarray(np.clip(pre, 0, 255).astype(np.uint8), "RGBA")
    # 分通道用 BOX 缩放（PIL 对 RGBA 会自动预乘，这里已手工预乘，所以按 4 个灰度通道各自缩放）
    chans = [np.asarray(Image.fromarray(np.clip(pre[..., i], 0, 255).astype(np.uint8)).resize((OUT_W, OUT_W), Image.BOX), np.float32) for i in range(4)]
    s = np.stack(chans, -1)[:OUT_H]
    a = s[..., 3:4] / 255.0
    rgb = np.where(a > 0, s[..., :3] / np.maximum(a, 1e-6), 0)
    return np.concatenate([np.clip(rgb, 0, 255), s[..., 3:4]], -1).astype(np.uint8)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("out"); ap.add_argument("--fps", type=int, default=12)
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    picks = {"phones_on": "phones_on_v2"}
    report = {}
    for name in ["idle_off", "phones_on", "phones_off", "tune", "groove", "listen", "sip", "love", "oops", "doze", "wave", "search", "sing"]:
        src = Path(a.src) / f"{picks.get(name, name)}.mp4"
        fr = np.stack([shrink(key(f)) for f in frames(src, a.fps)])
        np.savez_compressed(out / f"{name}.npz", frames=fr)
        report[name] = {"source": src.name, "frames": int(len(fr)), "size": [OUT_W, OUT_H]}
        print(name, fr.shape)
    (out / "frames.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
