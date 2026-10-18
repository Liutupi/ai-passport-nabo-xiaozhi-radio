// 扑克牌和绒布：纸面渐变、角标、按点数排布的花色、人头牌面板、带菱格纹和铜框的牌背、绒布纹理。
// 依赖主脚本里的 ctx、C、rr、text、F_MONO、F_DISP（调用时才取）。
'use strict';

const RANKS = ['', 'A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];
const SUITS = ['♠', '♥', '♣', '♦'];
// 2–10 的花色位置：(x, y) 是内框里的比例，下半部分的花色倒过来画
const PIP_LAYOUT = {
  2: [[.5, 0], [.5, 1]], 3: [[.5, 0], [.5, .5], [.5, 1]],
  4: [[0, 0], [1, 0], [0, 1], [1, 1]], 5: [[0, 0], [1, 0], [.5, .5], [0, 1], [1, 1]],
  6: [[0, 0], [1, 0], [0, .5], [1, .5], [0, 1], [1, 1]],
  7: [[0, 0], [1, 0], [.5, .25], [0, .5], [1, .5], [0, 1], [1, 1]],
  8: [[0, 0], [1, 0], [.5, .25], [0, .5], [1, .5], [.5, .75], [0, 1], [1, 1]],
  9: [[0, 0], [1, 0], [0, 1 / 3], [1, 1 / 3], [.5, .5], [0, 2 / 3], [1, 2 / 3], [0, 1], [1, 1]],
  10: [[0, 0], [1, 0], [.5, 1 / 6], [0, 1 / 3], [1, 1 / 3], [0, 2 / 3], [1, 2 / 3], [.5, 5 / 6], [0, 1], [1, 1]],
};

function suitGlyph(s, x, y, size, color, flip) {
  ctx.save(); ctx.translate(x, y); if (flip) ctx.rotate(Math.PI);
  text(SUITS[s], 0, 0, { size, color, align: 'center' });
  ctx.restore();
}

function cardShadow(x, y, w, h, r) {
  ctx.save();
  ctx.shadowColor = 'rgba(0,0,0,.45)'; ctx.shadowBlur = Math.max(2, w * 0.1); ctx.shadowOffsetX = w * 0.03; ctx.shadowOffsetY = w * 0.06;
  rr(x, y, w, h, r); ctx.fillStyle = '#000'; ctx.fill();
  ctx.restore();
}

function drawCardBack(x, y, w, h, r) {
  const g = ctx.createLinearGradient(x, y, x + w, y + h);
  g.addColorStop(0, '#8A3248'); g.addColorStop(1, '#561B2C');
  rr(x, y, w, h, r); ctx.fillStyle = g; ctx.fill();
  const m = Math.max(2, w * 0.1);
  ctx.save();
  rr(x + m, y + m, w - 2 * m, h - 2 * m, r * 0.6); ctx.clip();
  // 菱格纹
  ctx.strokeStyle = 'rgba(234,196,110,.28)'; ctx.lineWidth = Math.max(0.5, w * 0.018);
  const step = Math.max(4, w * 0.16);
  ctx.beginPath();
  for (let k = -h; k < w + h; k += step) { ctx.moveTo(x + k, y); ctx.lineTo(x + k + h, y + h); ctx.moveTo(x + k, y + h); ctx.lineTo(x + k + h, y); }
  ctx.stroke();
  ctx.restore();
  ctx.lineWidth = Math.max(0.8, w * 0.035); ctx.strokeStyle = C.brass;
  rr(x + m, y + m, w - 2 * m, h - 2 * m, r * 0.6); ctx.stroke();
  if (w >= 22) {
    // 中间的铜质圆章
    const cx = x + w / 2, cy = y + h / 2, rad = w * 0.24;
    const mg = ctx.createRadialGradient(cx - rad * 0.4, cy - rad * 0.4, rad * 0.1, cx, cy, rad);
    mg.addColorStop(0, '#F1D58A'); mg.addColorStop(1, '#A47B2C');
    ctx.beginPath(); ctx.arc(cx, cy, rad, 0, Math.PI * 2); ctx.fillStyle = mg; ctx.fill();
    ctx.lineWidth = 1; ctx.strokeStyle = '#3A2418'; ctx.stroke();
    text('N', cx, cy + rad * 0.08, { size: rad * 1.25, font: F_DISP, color: '#561B2C', align: 'center' });
  }
}

function drawCardFace(x, y, w, h, r, card) {
  const g = ctx.createLinearGradient(x, y, x, y + h);
  g.addColorStop(0, '#FFFDF7'); g.addColorStop(1, '#ECE5D4');
  rr(x, y, w, h, r); ctx.fillStyle = g; ctx.fill();
  const col = card.s % 2 ? C.red : C.ink, big = w >= 40;
  // 角标：左上，以及倒过来的右下
  const idx = (cx, cy, flip) => {
    ctx.save(); ctx.translate(cx, cy); if (flip) ctx.rotate(Math.PI);
    const rs = card.r === 10 ? w * 0.26 : w * 0.3;
    text(RANKS[card.r], 0, 0, { size: rs, weight: 700, font: F_MONO, color: col, align: 'center' });
    text(SUITS[card.s], 0, w * 0.25, { size: w * 0.22, color: col, align: 'center' });
    ctx.restore();
  };
  idx(x + w * 0.16, y + w * 0.2, false);
  if (big) idx(x + w * 0.84, y + h - w * 0.2, true);
  if (card.r >= 11) {
    // 人头牌：内框面板，大字母 + 花色
    const px = x + w * 0.28, py = y + h * 0.2, pw = w * 0.56, ph = h * 0.6;
    const pg = ctx.createLinearGradient(px, py, px, py + ph);
    pg.addColorStop(0, card.s % 2 ? '#F6D9D2' : '#DCE3EA'); pg.addColorStop(1, card.s % 2 ? '#EBC1B6' : '#C5D0DC');
    rr(px, py, pw, ph, w * 0.06); ctx.fillStyle = pg; ctx.fill();
    ctx.lineWidth = Math.max(0.8, w * 0.03); ctx.strokeStyle = C.brass; ctx.stroke();
    text(RANKS[card.r], px + pw / 2, py + ph * 0.42, { size: pw * 0.62, font: F_DISP, color: col, align: 'center' });
    suitGlyph(card.s, px + pw / 2, py + ph * 0.8, pw * 0.34, col);
  } else if (card.r === 1 || !big) {
    suitGlyph(card.s, x + w * 0.56, y + h * 0.6, w * (card.r === 1 ? 0.62 : 0.5), col);
  } else {
    const ix = x + w * 0.34, iy = y + h * 0.2, iw = w * 0.4, ih = h * 0.6;
    for (const [px, py] of PIP_LAYOUT[card.r]) suitGlyph(card.s, ix + px * iw, iy + py * ih, w * 0.2, col, py > 0.5);
  }
  // 细内框线 + 左上角一点纸面光泽
  ctx.lineWidth = 0.6; ctx.strokeStyle = 'rgba(20,16,14,.12)';
  rr(x + 1.5, y + 1.5, w - 3, h - 3, r * 0.7); ctx.stroke();
}

function drawCard(x, y, w, h, card) {
  const r = Math.max(2, w * 0.1);
  ctx.save();
  cardShadow(x, y, w, h, r);
  if (card) drawCardFace(x, y, w, h, r, card); else drawCardBack(x, y, w, h, r);
  rr(x, y, w, h, r); ctx.lineWidth = w > 30 ? 1.2 : 0.8; ctx.strokeStyle = 'rgba(20,16,14,.75)'; ctx.stroke();
  ctx.restore();
}

// f: 0 = 背面 → 1 = 正面；翻到一半时牌略微抬起
function drawCardFlip(x, y, w, h, card, f) {
  const t = Math.max(0, Math.min(1, f)), sx = Math.abs(Math.cos(Math.PI * t)), lift = Math.sin(Math.PI * t);
  ctx.save();
  ctx.translate(x + w / 2, y + h / 2 - lift * 4); ctx.scale(Math.max(sx, 0.02) * (1 + lift * 0.08), 1 + lift * 0.08);
  drawCard(-w / 2, -h / 2, w, h, t < 0.5 ? null : card);
  ctx.restore();
}

// 绒布：暗角 + 细密的绒毛颗粒，只生成一次
const FELT = (() => {
  let tex = null;
  function make() {
    const cv = document.createElement('canvas'); cv.width = 480; cv.height = 640;
    const g = cv.getContext('2d'), img = g.createImageData(480, 640), d = img.data;
    let s = 1234567;
    const rnd = () => { s = (s * 1103515245 + 12345) >>> 0; return (s >>> 8) / 16777216; };
    for (let y = 0; y < 640; y++) for (let x = 0; x < 480; x++) {
      const dx = (x - 240) / 300, dy = (y - 380) / 380, vig = Math.min(1, Math.hypot(dx, dy));
      const t = 1 - vig * 0.75, n = (rnd() - 0.5) * 14 + ((x * 7 + y * 3) % 5 === 0 ? 4 : 0);
      const k = (y * 480 + x) * 4;
      d[k] = 12 + 24 * t + n * 0.5; d[k + 1] = 48 + 64 * t + n; d[k + 2] = 40 + 46 * t + n * 0.8; d[k + 3] = 255;
    }
    g.putImageData(img, 0, 0);
    return cv;
  }
  return { draw(g, y0) { tex = tex || make(); g.drawImage(tex, 0, y0 * 2, 480, (320 - y0) * 2, 0, y0, 240, 320 - y0); } };
})();
