// 音效：16 kHz 离线合成后播放，和徽章的采样率一致。
// 骰子落桌用固件摇骰子 app 的阻尼共振器（nabo_dice_render.c 的 sound 部分）：
// 每次撞击是一段 4 ms 的噪声，激励 4 个共振器（约 1150 / 2650 / 4100 / 520 Hz）。
'use strict';

const SOUND = (() => {
  const SR = 16000;
  const A1 = [29399, 16476, -1273, 32020].map(v => v / 16384);
  const A2 = [16291, 16199, 16046, 16316].map(v => v / 16384);
  const GAIN = [9, 12, 10, 4];
  let ac = null;
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

  function ctx() {
    if (typeof Cfg === 'undefined' || !Cfg.sound) return null;
    try { ac = ac || new AudioContext(); if (ac.state === 'suspended') ac.resume(); return ac; } catch { return null; }
  }
  // 播放一段 16 kHz 单声道采样，可选一个滤波器
  function play(data, o = {}) {
    const a = ctx(); if (!a || !data.length) return;
    const buf = a.createBuffer(1, data.length, SR); buf.copyToChannel(data, 0);
    const src = a.createBufferSource(); src.buffer = buf;
    let node = src;
    if (o.filter) {
      const f = a.createBiquadFilter(); f.type = o.filter; f.frequency.value = o.freq; f.Q.value = o.q || 0.7;
      node.connect(f); node = f;
    }
    const g = a.createGain(); g.gain.value = o.gain ?? 1;
    node.connect(g).connect(a.destination);
    src.start(a.currentTime + (o.delay || 0));
  }

  // 一串撞击 → 木质敲击声。hits: [{ms, amp}]，amp 0–256（固件里约等于落地速度）
  function knocks(hits, tailMs = 280) {
    const list = [...hits].sort((x, y) => x.ms - y.ms);
    const n = Math.ceil(((list.length ? list[list.length - 1].ms : 0) + tailMs) * SR / 1000);
    const out = new Float32Array(n), y = [[0, 0], [0, 0], [0, 0], [0, 0]];
    let next = 0, burst = 0, amp = 0;
    for (let i = 0; i < n; i++) {
      while (next < list.length && list[next].ms * 16 <= i) { burst = 64; amp = list[next++].amp; }
      let x = 0;
      if (burst > 0) { x = (Math.random() * 4096 - 2048) * (amp * burst / 64) / 16; burst--; }
      let mix = 0;
      for (let k = 0; k < 4; k++) {
        const v = A1[k] * y[k][0] - A2[k] * y[k][1] + x / 16;
        y[k][1] = y[k][0]; y[k][0] = clamp(v, -4194304, 4194304);
        mix += v * GAIN[k];
      }
      mix = mix * 5 / 64;
      if (mix > 24000) mix = 24000 + (mix - 24000) / 4;
      if (mix < -24000) mix = -24000 + (mix + 24000) / 4;
      out[i] = clamp(mix, -32000, 32000) / 32768;
    }
    return out;
  }
  // 带包络的噪声：纸牌摩擦、骰盅掀开
  function noise(ms, attackMs, decay = 3) {
    const n = Math.ceil(ms * SR / 1000), a = Math.max(1, attackMs * SR / 1000), out = new Float32Array(n);
    for (let i = 0; i < n; i++) out[i] = (Math.random() * 2 - 1) * Math.min(1, i / a) * Math.pow(1 - i / n, decay);
    return out;
  }
  // 衰减的正弦和泛音：铃声
  function bell(freq, ms, vol = 0.25) {
    const n = Math.ceil(ms * SR / 1000), out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const t = i / SR, env = Math.exp(-t * 7) * Math.min(1, i / 40);
      out[i] = vol * env * (Math.sin(2 * Math.PI * freq * t) + 0.35 * Math.sin(2 * Math.PI * freq * 2.76 * t) * Math.exp(-t * 10));
    }
    return out;
  }
  function thump(freq, ms, vol = 0.6) {
    const n = Math.ceil(ms * SR / 1000), out = new Float32Array(n);
    for (let i = 0; i < n; i++) { const t = i / SR; out[i] = vol * Math.exp(-t * 28) * Math.sin(2 * Math.PI * freq * (1 - t * 1.5) * t); }
    return out;
  }
  const rnd = (lo, hi) => lo + Math.random() * (hi - lo);

  const SFX = {
    // 界面：很轻的木质滴答
    move: () => play(knocks([{ ms: 0, amp: 20 }], 80), { gain: 0.5 }),
    ok: () => play(knocks([{ ms: 0, amp: 46 }, { ms: 12, amp: 16 }], 120), { gain: 0.6 }),
    // 发牌：牌面擦过绒布，最后轻轻落桌
    card: () => {
      play(noise(95, 6, 2.2), { filter: 'bandpass', freq: 3200, q: 0.9, gain: 0.55 });
      play(knocks([{ ms: 0, amp: 14 }], 60), { gain: 0.35, delay: 0.08 });
    },
    // 翻牌：短促的一声"啪"
    flip: () => {
      play(noise(28, 1, 4), { filter: 'highpass', freq: 2200, gain: 0.7 });
      play(knocks([{ ms: 0, amp: 24 }], 60), { gain: 0.4, delay: 0.012 });
    },
    // 洗牌：一串越来越密的纸牌拨动声，最后压实
    shuffle: () => {
      const clicks = [];
      for (let i = 0; i < 46; i++) clicks.push(640 * Math.pow(i / 46, 0.8));
      const n = Math.ceil(760 * SR / 1000), out = new Float32Array(n);
      for (const ms of clicks) {
        const s0 = Math.floor(ms * SR / 1000), len = 40, a = rnd(0.25, 0.6);
        for (let i = 0; i < len && s0 + i < n; i++) out[s0 + i] += (Math.random() * 2 - 1) * a * (1 - i / len);
      }
      play(out, { filter: 'highpass', freq: 1800, gain: 0.5 });
      play(knocks([{ ms: 0, amp: 70 }, { ms: 18, amp: 30 }], 120), { gain: 0.5, delay: 0.68 });
    },
    // 摇盅：骰子在盅里乱撞
    rattle: (ms = 1050) => {
      const hits = []; let t = 0;
      while (t < ms) { hits.push({ ms: t, amp: rnd(40, 140) }); if (Math.random() < 0.35) hits.push({ ms: t + rnd(6, 16), amp: rnd(20, 60) }); t += rnd(28, 70); }
      play(knocks(hits, 200), { gain: 0.45 });
    },
    // 扣盅：重重一放，骰子在里面最后弹两下
    slam: () => {
      play(knocks([{ ms: 0, amp: 256 }, { ms: 16, amp: 110 }, { ms: 52, amp: 60 }, { ms: 95, amp: 26 }], 300), { gain: 0.6 });
      play(thump(95, 180), { gain: 0.8 });
    },
    // 骰子落桌（单颗）：和固件的弹跳节奏一致
    land: (amp = 200) => play(knocks([{ ms: 0, amp }, { ms: 20, amp: amp * 0.45 }, { ms: 140, amp: amp * 0.22 }, { ms: 215, amp: amp * 0.1 }], 200), { gain: 0.55 }),
    // 开！骰盅掀开或牌桌亮牌
    open: () => {
      play(noise(220, 30, 1.6), { filter: 'lowpass', freq: 1400, gain: 0.5 });
      play(bell(784, 600), { delay: 0.05 });
      play(bell(1175, 700), { delay: 0.14 });
    },
    lift: () => play(noise(260, 60, 1.4), { filter: 'bandpass', freq: 900, q: 0.6, gain: 0.6 }),
    win: () => [523, 659, 784, 1047].forEach((f, i) => play(bell(f, 700, 0.22), { delay: i * 0.09 })),
  };
  return { SFX, knocks };
})();
const SFX = SOUND.SFX;
