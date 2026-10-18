// 立体骰子：移植自固件摇骰子 app 的 nabo_dice_render.c（浮点版）。
// 正交投影、47° 俯视、左上方光源 + Blinn 高光、圆角倒边、中式红色 1 点和 4 点、凹坑点。
// 每颗骰子按（点数、朝向、尺寸）渲染一次后缓存成离屏画布。
'use strict';

const DICE3D = (() => {
  // 固件摇骰子用 47°；这里要一眼读出朝上的点数，俯视更多一些
  const TILT = 58 * Math.PI / 180;
  const VIS_Z = 600 / 16384;            // 接近侧对视线的面不画
  const BEVEL = 0.74;                   // 从 0.74 开始是圆角倒边
  const PIP_STEP = 0.53, PIP_SPLIT = 0.265, PIP_R = 0.177, PIP_R_ONE = 0.28;
  const PIPS = [0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d];
  const L = [-6891, 11813, 9024].map(v => v / 16384);
  const HV = [-3913, 6707, 14426].map(v => v / 16384);
  const IVORY = [247, 242, 231], COOL = [-10, -3, 12];
  const INK = [[30, 30, 36], [206, 26, 34]];
  // 把每个点数转到朝上（+Y）的带符号置换。局部面：1 +Z, 6 -Z, 2 +X, 5 -X, 3 +Y, 4 -Y
  const TO_TOP = [null,
    [1, 0, 0, 0, 0, 1, 0, -1, 0], [0, -1, 0, 1, 0, 0, 0, 0, 1], [1, 0, 0, 0, 1, 0, 0, 0, 1],
    [1, 0, 0, 0, -1, 0, 0, 0, -1], [0, 1, 0, -1, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, -1, 0, 1, 0]];
  // 点数、法线轴、符号、u 轴、v 轴
  const FACES = [[1, 2, 1, 0, 1], [6, 2, -1, 1, 0], [2, 0, 1, 1, 2], [5, 0, -1, 2, 1], [3, 1, 1, 2, 0], [4, 1, -1, 0, 2]];
  const cache = new Map();

  const mul = (a, b) => {
    const o = new Array(9);
    for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) o[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    return o;
  };
  const col = (m, i) => [m[i], m[3 + i], m[6 + i]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cl = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

  function shade(n) {
    const dif = Math.max(0, dot(n, L)), nh = Math.max(0, dot(n, HV));
    const light = 0.39 + 0.67 * dif, spec = Math.pow(nh, 32) * 0.4517;
    return { c: IVORY.map((v, i) => v * light + 255 * spec + COOL[i] * (1 - dif) / 4), lit: light };
  }

  function buildFaces(value, yaw, s) {
    const cy = Math.cos(yaw), sy = Math.sin(yaw), ce = Math.cos(TILT), se = Math.sin(TILT);
    const world = mul([cy, 0, sy, 0, 1, 0, -sy, 0, cy], TO_TOP[value]);
    const view = mul([1, 0, 0, 0, ce, -se, 0, se, ce], world);
    const faces = [];
    for (const [v, ni, ns, ai, bi] of FACES) {
      const n = col(view, ni).map(x => x * ns);
      if (n[2] <= VIS_Z) continue;
      const a = col(view, ai), b = col(view, bi);
      const a2 = [a[0] * s, -a[1] * s], b2 = [b[0] * s, -b[1] * s];
      const det = a2[0] * b2[1] - a2[1] * b2[0];
      if (Math.abs(det) < 1e-6) continue;
      const gu = 1 / Math.hypot(b2[1] / det, b2[0] / det), gv = 1 / Math.hypot(a2[1] / det, a2[0] / det);
      const base = shade(n);
      const edges = [a, a.map(x => -x), b, b.map(x => -x)].map(e => {
        const m = n.map((x, i) => x + e[i]), len = Math.hypot(...m);
        return shade(m.map(x => x / len)).c.map((x, i) => x - base.c[i]);
      });
      faces.push({
        value: v, cx: n[0] * s, cy: -n[1] * s, a2, b2, det, gu, gv,
        sil: [a[2] <= VIS_Z, -a[2] <= VIS_Z, b[2] <= VIS_Z, -b[2] <= VIS_Z],
        col: base.c, lit: base.lit, edges, lu: dot(a, L), lv: dot(b, L),
      });
    }
    return { faces, ex: s * (Math.abs(view[0]) + Math.abs(view[1]) + Math.abs(view[2])), ey: s * (Math.abs(view[3]) + Math.abs(view[4]) + Math.abs(view[5])) };
  }

  function pip(f, u, v, c) {
    const gx = u > PIP_SPLIT ? 2 : u < -PIP_SPLIT ? 0 : 1, gy = v > PIP_SPLIT ? 2 : v < -PIP_SPLIT ? 0 : 1;
    if (!(PIPS[f.value] & (1 << (gy * 3 + gx)))) return;
    const r = f.value === 1 ? PIP_R_ONE : PIP_R;
    const pu = u - (gx - 1) * PIP_STEP, pv = v - (gy - 1) * PIP_STEP, d = Math.hypot(pu, pv);
    const pc = cl((r - d) * (f.gu + f.gv) / 2 + 0.5, 0, 1);
    if (!pc) return;
    // 凹坑：靠近光源一侧的坑壁在阴影里
    const conc = cl(1 - (pu * f.lu + pv * f.lv) / r * 0.43, 0.586, 1.48);
    const ink = INK[f.value === 1 || f.value === 4 ? 1 : 0];
    for (let i = 0; i < 3; i++) c[i] += (ink[i] * f.lit * conc - c[i]) * pc;
  }

  function facePixel(f, dx, dy, out) {
    const u = (dx * f.b2[1] - dy * f.b2[0]) / f.det, v = (dy * f.a2[0] - dx * f.a2[1]) / f.det;
    const au = Math.abs(u), av = Math.abs(v);
    if (au > 2 || av > 2) return 0;
    const du = (1 - au) * f.gu, dv = (1 - av) * f.gv;
    if (du < -0.5 || dv < -0.5) return 0;
    let cov = 1;
    if (f.sil[u >= 0 ? 0 : 1]) cov = cl(du + 0.5, 0, 1);
    if (f.sil[v >= 0 ? 2 : 3]) cov *= cl(dv + 0.5, 0, 1);
    if (!cov) return 0;
    const tu = au > BEVEL ? Math.min(1, (au - BEVEL) / (1 - BEVEL)) : 0;
    const tv = av > BEVEL ? Math.min(1, (av - BEVEL) / (1 - BEVEL)) : 0;
    const eu = f.edges[u >= 0 ? 0 : 1], ev = f.edges[v >= 0 ? 2 : 3];
    for (let i = 0; i < 3; i++) out[i] = f.col[i] + eu[i] * tu + ev[i] * tv;
    pip(f, u, v, out);
    return cov;
  }

  function render(value, yaw, s) {
    const { faces, ex, ey } = buildFaces(value, yaw, s);
    const w = Math.ceil(ex * 2 + 4), h = Math.ceil(ey * 2 + 4);
    const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
    const g = cv.getContext('2d'), img = g.createImageData(w, h), px = img.data;
    const c = [0, 0, 0];
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const dx = x + 0.5 - w / 2, dy = y + 0.5 - h / 2;
      let best = 0, rgb = null;
      for (const f of faces) {
        const cov = facePixel(f, dx - f.cx, dy - f.cy, c);
        if (cov > best) { best = cov; rgb = [c[0], c[1], c[2]]; }
        if (best >= 1) break;
      }
      if (!best) continue;
      const k = (y * w + x) * 4;
      px[k] = cl(rgb[0], 0, 255); px[k + 1] = cl(rgb[1], 0, 255); px[k + 2] = cl(rgb[2], 0, 255); px[k + 3] = best * 255;
    }
    g.putImageData(img, 0, 0);
    return cv;
  }

  // 在 240×320 坐标里画一颗立体骰子：(x, y, size) 是占位方框，yaw 是绕竖轴的角度（度）
  function draw(g, x, y, size, value, yawDeg = 22, o = {}) {
    const dpr = 2, s = size / 2.7;
    const key = `${value}|${Math.round(yawDeg)}|${Math.round(s * dpr * 4)}`;
    let im = cache.get(key);
    if (!im) { im = render(value, yawDeg * Math.PI / 180, s * dpr); cache.set(key, im); }
    const cx = x + size / 2, cy = y + size / 2;
    g.save();
    if (o.dim) g.globalAlpha = 0.32;
    // 接触阴影：光在左上，影子落在右下
    const sh = g.createRadialGradient(cx + s * 0.34, cy + s * 0.7, 0, cx + s * 0.34, cy + s * 0.7, s * 1.5);
    sh.addColorStop(0, 'rgba(0,0,0,.55)'); sh.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = sh; g.beginPath(); g.ellipse(cx + s * 0.34, cy + s * 0.7, s * 1.5, s * 0.75, 0, 0, Math.PI * 2); g.fill();
    if (o.hl) {
      const glow = g.createRadialGradient(cx, cy, s * 0.6, cx, cy, s * 1.9);
      glow.addColorStop(0, o.hl); glow.addColorStop(1, 'rgba(201,162,74,0)');
      g.globalAlpha = 0.7; g.fillStyle = glow; g.beginPath(); g.arc(cx, cy, s * 1.9, 0, Math.PI * 2); g.fill(); g.globalAlpha = 1;
    }
    g.drawImage(im, cx - im.width / dpr / 2, cy - im.height / dpr / 2, im.width / dpr, im.height / dpr);
    g.restore();
  }

  // 同一局里每颗骰子的随机朝向：固件也是 90° 的倍数加减 15°–31°
  const randomYaw = rnd => Math.floor(rnd() * 4) * 90 + (rnd() < 0.5 ? -1 : 1) * (15 + rnd() * 16);

  return { draw, randomYaw };
})();
