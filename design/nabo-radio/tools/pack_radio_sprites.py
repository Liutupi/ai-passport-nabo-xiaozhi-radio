#!/usr/bin/env python3
"""NABO DJ 动作帧 -> radio_sprites.bin（资源分区，固件原地读取）。

每个动作：一套调色板（索引 0 = 透明，最多 63 色，RGB565），第 0 帧为关键帧，其余帧相对**上一帧**编码。
固件播放时保留一块 w×h 字节的索引缓冲（158×139 ≈ 22 KB，只在电台页存在），顺序解码；循环回到第 0 帧时重新解关键帧。
半透明边缘预先和舞台色混合；颜色和当前显示相近的像素记为“不变”，同时去掉视频的细微闪烁。

片段选择：待机 / 点头 / 跟唱自动找从静止出发、回到静止的 1.5–3 秒；拧旋钮 / 收听 / 打瞌睡 / 找信号和单次动作一样整条使用（8 fps）；单次动作裁掉首尾静止部分（8 fps，最多 24 帧，长动作按比例加快）。

行编码（每行独立，行首有偏移表）：
  0b00nnnnnn  跳过 n+1 像素：关键帧里表示透明，差分帧里表示“同上一帧”
  0b01nnnnnn  接下来 n+1 个字节是调色板索引（逐个）
  0b10nnnnnn  下一个字节的索引重复 n+1 次
  0b11000000  行结束
文件格式见 write_pack()；脚本最后会把每一帧解码回来，和量化后的帧逐像素比对。
用法：python tools/pack_radio_sprites.py <帧目录> <输出.bin> [--fps 12] [--report report.json]
"""
import argparse, json, struct
from pathlib import Path
import numpy as np
from PIL import Image

STAGE = np.array([0x11, 0x17, 0x25], np.float32)
ACTIONS = ["idle_off", "phones_on", "phones_off", "tune", "groove", "listen", "sip", "love",
           "oops", "doze", "wave", "search", "sing"]
LOOP = {"idle_off", "tune", "groove", "listen", "doze", "search", "sing"}
# 持续的小动作：截一段从静止出发、回到静止的循环。其余循环动作是完整手势（Grok 首尾钉在起始图），整条压缩使用。
SEGMENT_LOOP = {"idle_off", "groove", "sing"}
SAME_TOL = 30.0      # 与当前显示颜色距离小于此值视为未变（RGB 欧氏距离）
ONE_SHOT_MAX = 20    # 单次动作最多 24 帧，8 fps 播放 3 秒；Grok 动作偏慢，长的按比例加快



def rgb565(c):
    r, g, b = (int(x) for x in c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def flatten(fr):
    """RGBA -> (RGB 预混合舞台色, 是否不透明)。alpha < 50% 视为透明。"""
    a = fr[..., 3:4].astype(np.float32) / 255
    rgb = fr[..., :3].astype(np.float32) * a + STAGE * (1 - a)
    return rgb, fr[..., 3] >= 128


def palette_for(frames_rgb, masks, colors=63):
    pix = np.concatenate([f[m] for f, m in zip(frames_rgb, masks)])
    rng = np.random.default_rng(1)
    if len(pix) > 200000:
        pix = pix[rng.choice(len(pix), 200000, replace=False)]
    side = int(np.ceil(np.sqrt(len(pix))))
    pad = np.zeros((side * side, 3), np.float32)
    pad[:len(pix)] = pix
    pad[len(pix):] = pix[0]
    im = Image.fromarray(pad.reshape(side, side, 3).astype(np.uint8))
    q = im.quantize(colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[:colors * 3], np.float32).reshape(-1, 3)
    return pal


def index(rgb, mask, pal):
    d = ((rgb[..., None, :] - pal[None, None]) ** 2).sum(-1)
    idx = d.argmin(-1).astype(np.uint8) + 1
    idx[~mask] = 0
    return idx


def encode_row(cur, ref):
    """ref is None for keyframes (skip = transparent); otherwise skip = same as ref."""
    out, x, w = bytearray(), 0, len(cur)
    skip = (cur == 0) if ref is None else (cur == ref)
    while x < w:
        if skip[x]:
            n = 1
            while x + n < w and skip[x + n] and n < 64:
                n += 1
            out.append(n - 1)
            x += n
            continue
        n = 1
        while x + n < w and not skip[x + n] and cur[x + n] == cur[x] and n < 64:
            n += 1
        if n >= 3:
            out += bytes([0x80 | (n - 1), cur[x]])
            x += n
            continue
        n = 1
        while x + n < w and not skip[x + n] and n < 64:
            if x + n + 2 < w and cur[x + n] == cur[x + n + 1] == cur[x + n + 2] and not skip[x + n + 1] and not skip[x + n + 2]:
                break
            n += 1
        out.append(0x40 | (n - 1))
        out += bytes(cur[x:x + n])
        x += n
    # trailing skips are implicit
    while out and (out[-1] & 0xC0) == 0 and _trailing_skip_only(out):
        break
    out.append(0xC0)
    return bytes(out)


def _trailing_skip_only(_):
    return False


def decode_row(data, at, w, ref_row):
    row = np.zeros(w, np.uint8) if ref_row is None else ref_row.copy()
    x = 0
    while True:
        c = data[at]; at += 1
        op, n = c >> 6, (c & 63) + 1
        if c == 0xC0:
            break
        if op == 0:
            x += n
        elif op == 1:
            row[x:x + n] = np.frombuffer(data[at:at + n], np.uint8); at += n; x += n
        elif op == 2:
            row[x:x + n] = data[at]; at += 1; x += n
    return row


def frame_diff(a, b):
    m = (a[..., 3] > 128) | (b[..., 3] > 128)
    return float(np.abs(a[..., :3].astype(np.int16) - b[..., :3].astype(np.int16))[m].mean()) if m.any() else 0.0


def choose(name, fr):
    """返回要用的源帧序号（12 fps 源）和播放 fps。"""
    n = len(fr)
    if name in SEGMENT_LOOP:
        best = None
        rest = [frame_diff(fr[k], fr[0]) for k in range(n)]   # 和静止姿势（起始图）差多少
        for L in range(18, 37):
            for a in range(0, n - L):
                motion = np.mean([frame_diff(fr[a + k], fr[a + k + 1]) for k in range(0, L, 3)])
                # 首尾都要回到静止姿势：任意两个状态之间切换不跳；中间要有动作
                score = rest[a] + rest[a + L] + frame_diff(fr[a], fr[a + L]) - 0.3 * motion
                if best is None or score < best[0]:
                    best = (score, a, L)
        _, a, L = best
        return [int(round(x)) for x in np.arange(a, a + L, 1.5)], 8
    motion = np.array([frame_diff(fr[k], fr[k + 1]) for k in range(n - 1)])
    active = np.where(motion > max(1.5, motion.max() * 0.15))[0]
    a, b = (int(active[0]), int(active[-1]) + 2) if len(active) else (0, n)
    a, b = max(0, a - 1), min(n, b + 2)
    if b - a > ONE_SHOT_MAX * 1.5:      # 太长就跳着取，保持起止不变
        return [int(round(x)) for x in np.linspace(a, b - 1, ONE_SHOT_MAX)], 8
    return [int(round(x)) for x in np.arange(a, b, 1.5)], 8


def build(frame_dir, fps_keep=None, src_fps=12):
    actions = []
    for name in ACTIONS:
        fr = np.load(Path(frame_dir) / f"{name}.npz")["frames"][:-1]   # 末帧钉回起始图，与第 0 帧重复
        pick, fps = choose(name, fr)
        fr = fr[pick]
        flat = [flatten(f) for f in fr]
        rgbs, masks = [f[0] for f in flat], [f[1] for f in flat]
        pal = palette_for(rgbs, masks)
        idxs = [index(r, m, pal) for r, m in zip(rgbs, masks)]
        # 去闪烁：与当前显示颜色接近（且都不透明）的像素保持不变
        shown, shown_rgb = idxs[0].copy(), rgbs[0].copy()
        for k in range(1, len(idxs)):
            same = masks[k] & (shown > 0) & (np.sqrt(((rgbs[k] - shown_rgb) ** 2).sum(-1)) < SAME_TOL)
            idxs[k] = np.where(same, shown, idxs[k]).astype(np.uint8)
            changed = idxs[k] != shown
            shown_rgb = np.where(changed[..., None], rgbs[k], shown_rgb)
            shown = idxs[k]
        h, w = idxs[0].shape
        frames = [[encode_row(idxs[k][y], None if k == 0 else idxs[k - 1][y]) for y in range(h)]
                  for k in range(len(idxs))]
        actions.append(dict(name=name, pal=pal, idxs=idxs, frames=frames, w=w, h=h, fps=fps,
                            loop=name in LOOP, source_frames=pick))
    return actions


def write_pack(actions, path):
    """'NFM1' | u16 action_count | u16 0 | action table (32 B each) | data
       action: char name[12] | u16 w | u16 h | u8 fps | u8 loop | u16 frames | u32 palette_off | u32 frames_off | u32 0
       palette: 64 × u16 RGB565 (entry 0 unused)
       frames_off -> u32 frame_off[frames]; frame: u16 row_off[h] (from frame start, after the table) | rows"""
    head = bytearray(b"NFM1" + struct.pack("<HH", len(actions), 0))
    table = bytearray()
    body = bytearray()
    base = len(head) + 32 * len(actions)
    for a in actions:
        pal_off = base + len(body)
        pal = [0] + [rgb565(c) for c in a["pal"]]
        pal += [0] * (64 - len(pal))
        body += struct.pack("<64H", *pal)
        frames_off = base + len(body)
        offs_at = len(body)
        body += bytes(4 * len(a["frames"]))
        for k, rows in enumerate(a["frames"]):
            struct.pack_into("<I", body, offs_at + 4 * k, base + len(body))
            row_table = bytearray()
            data = bytearray()
            for r in rows:
                row_table += struct.pack("<H", 2 * a["h"] + len(data))
                data += r
            assert 2 * a["h"] + len(data) < 65536
            body += row_table + data
        name = a["name"].encode()[:12].ljust(12, b"\0")
        table += name + struct.pack("<HHBBHIII", a["w"], a["h"], a["fps"], a["loop"], len(a["frames"]),
                                    pal_off, frames_off, 0)
    blob = bytes(head + table + body)
    Path(path).write_bytes(blob)
    return blob


def verify(blob, actions):
    n = struct.unpack_from("<H", blob, 4)[0]
    for i in range(n):
        name = blob[8 + 32 * i:20 + 32 * i].rstrip(b"\0").decode()
        w, h, fps, loop, nf, pal_off, frames_off, _ = struct.unpack_from("<HHBBHIII", blob, 20 + 32 * i)
        a = actions[i]
        assert name == a["name"] and nf == len(a["idxs"])
        prev = None
        for k in range(nf):
            fo = struct.unpack_from("<I", blob, frames_off + 4 * k)[0]
            img = np.zeros((h, w), np.uint8)
            for y in range(h):
                ro = struct.unpack_from("<H", blob, fo + 2 * y)[0]
                img[y] = decode_row(blob, fo + ro, w, None if k == 0 else prev[y])
            prev = img
            assert np.array_equal(img, a["idxs"][k]), (name, k)
    return True


def sheet(actions, path, frames_per_row=12):
    rows = []
    for a in actions:
        pal = np.vstack([STAGE, a["pal"]]).astype(np.uint8)
        pick = np.linspace(0, len(a["idxs"]) - 1, frames_per_row).round().astype(int)
        rows.append(np.hstack([pal[a["idxs"][k]] for k in pick]))
    Image.fromarray(np.vstack(rows)).save(path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("frames"); ap.add_argument("out")
    ap.add_argument("--report")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    actions = build(a.frames)
    blob = write_pack(actions, a.out)
    verify(blob, actions)
    report = {"bytes": len(blob), "actions": {}}
    for x in actions:
        key = sum(len(r) for r in x["frames"][0])
        rest = sum(len(r) for f in x["frames"][1:] for r in f)
        report["actions"][x["name"]] = {"frames": len(x["frames"]), "fps": x["fps"], "loop": x["loop"],
                                        "source_frames_12fps": [x["source_frames"][0], x["source_frames"][-1]],
                                        "colors": len(x["pal"]),
                                        "key_bytes": key, "delta_bytes_avg": rest // max(1, len(x["frames"]) - 1)}
    print(json.dumps(report, ensure_ascii=False, indent=1))
    if a.report:
        Path(a.report).write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    if a.sheet:
        sheet(actions, a.sheet)
    print("verified: every frame decodes back to its quantised indices")
