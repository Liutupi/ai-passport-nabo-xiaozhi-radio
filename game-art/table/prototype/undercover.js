// 谁是卧底（3–8 人）：设置人数 → 逐个交接看词 → 轮流描述 → 在工牌上投票出局 → 揭晓。
// 规则和随机数的取用顺序与固件 nabo_table_uc.c 一致，test_table.py 逐步对拍。
const UC_MIN = 3, UC_MAX = 8, UC_RECENT = 24;
const UC_COL_EXTRA = ['#FFD65C', '#B28CFF', '#48DCC8', '#FF8080'];
const ucCol = p => (p < 4 ? PCOL[p] : UC_COL_EXTRA[p - 4]);
const ucName = p => `玩家 ${p + 1}`;

function ucSetup() {
  G.game = 'uc'; G.menu = 2;   // 直接打开卧底（语音）也让封面光标停在「谁是卧底」
  if (!G.uc) G.uc = { recent: [] };
  G.uc.cursor = Cfg.ucPlayers;
  go('ucSetup'); naboPlay('idle');
}
const ucWord = p => UC_WORDS[G.uc.pair][G.uc.uc[p] === G.uc.swap ? 1 : 0];
const ucAlive = side => G.uc.alive.filter((a, p) => a && G.uc.uc[p] === side).length;
const ucListLen = () => ucAlive(false) + ucAlive(true) + 1;
function ucListPlayer(row) {
  for (let p = 0; p < G.uc.n; p++) if (G.uc.alive[p] && row-- === 0) return p;
  return -1;
}
function ucDeal() {
  const u = G.uc;
  let pair = randInt(UC_WORDS.length);
  for (let tries = 0; tries < 64 && u.recent.includes(pair); tries++) pair = randInt(UC_WORDS.length);
  if (u.recent.length === UC_RECENT) u.recent.shift();
  u.recent.push(pair);
  u.pair = pair; u.swap = rand() < 0.5; u.nUc = u.n >= 7 ? 2 : 1;
  u.uc = Array(u.n).fill(false);
  for (let k = 0; k < u.nUc; k++) {
    let j = randInt(u.n - k);
    for (let p = 0; p < u.n; p++) if (!u.uc[p] && j-- === 0) { u.uc[p] = true; break; }
  }
  u.alive = Array(u.n).fill(true);
  u.round = 1; u.out = -1; u.speaker = -1; u.recheck = false; u.confirm = false;
  G.round++;
  log(`谁是卧底第 ${G.round} 局：${u.n} 人，${u.nUc} 个卧底`);
  handoff(0);
}
function ucToTalk(newRound) {
  const u = G.uc;
  if (newRound) {
    if (u.speaker < 0) u.speaker = ucListPlayer(randInt(ucAlive(false) + ucAlive(true)));
    else if (u.out >= 0 && !u.alive[u.speaker]) { let p = u.speaker; do p = (p + 1) % u.n; while (!u.alive[p]); u.speaker = p; }
  }
  u.cursor = 0; u.confirm = false;
  go('ucTalk'); naboPlay('idle');
}
function ucBeginTurn(p) { G.turn = p; go('ucWord'); naboPlay('idle'); }
function ucVoteOut(p) {
  const u = G.uc;
  u.alive[p] = false; u.out = p;
  const civ = ucAlive(false), und = ucAlive(true);
  G.result = civ === 0 || und === 0 || und >= civ ? { uc: true, winners: 0, loser: -1 } : null;
  u.ucWon = und > 0;
  go('ucOut'); naboPlay(u.uc[p] ? 'surprise' : 'point', { mirror: !u.uc[p] && p % 2 === 1 });
  log(`${ucName(p)} 出局：${u.uc[p] ? '是卧底' : '是平民'}`);
}
function ucKey(k) {
  const u = G.uc, move = k === 'up' || k === 'down';
  if (k !== 'ok' && !move) return;
  move ? SFX.move() : SFX.ok();
  switch (G.scene) {
    case 'ucSetup':
      if (k === 'up' && u.cursor < UC_MAX) u.cursor++;
      else if (k === 'down' && u.cursor > UC_MIN) u.cursor--;
      else if (k === 'ok') { Cfg.ucPlayers = u.cursor; save('table.settings', Cfg); u.n = u.cursor; u.recent = []; G.round = 0; ucDeal(); }
      break;
    case 'ucWord':
      if (k !== 'ok') break;
      if (u.recheck) { u.recheck = false; ucToTalk(false); }
      else if (G.turn + 1 < u.n) handoff(G.turn + 1);
      else ucToTalk(true);
      break;
    case 'ucTalk':
      if (move) { u.cursor ^= 1; break; }
      u.list = u.cursor ? 'forgot' : 'vote'; u.cursor = 0; u.confirm = false;
      go('ucVote'); naboPlay('idle');
      break;
    case 'ucVote': {
      if (u.confirm) {
        if (move) { u.confirmCursor ^= 1; break; }
        u.confirm = false;
        if (u.confirmCursor === 0) ucVoteOut(ucListPlayer(u.cursor));
        break;
      }
      if (move) { u.cursor = Math.max(0, Math.min(ucListLen() - 1, u.cursor + (k === 'down' ? 1 : -1))); break; }
      const p = ucListPlayer(u.cursor);
      if (u.list === 'forgot') { if (p < 0) ucToTalk(false); else { u.recheck = true; handoff(p); } }
      else if (p < 0) { u.round++; ucToTalk(false); }
      else { u.confirm = true; u.confirmCursor = 0; }
      break;
    }
    case 'ucOut':
      if (k !== 'ok') break;
      if (G.result) { G.choice = 0; go('ucEnd'); naboPlay('trophy'); SFX.win(); }
      else { u.round++; ucToTalk(true); }
      break;
    case 'ucEnd':
      if (move) { G.choice ^= 1; break; }
      if (G.choice === 1) { toMenu(); break; }
      G.result = null; ucDeal();
      break;
  }
}

// ---------- 画面（布局与固件一致，字体用浏览器的） ----------
function ucChip(x, y, p, out, sel, tag, tagCol) {
  rr(x, y - 10, 104, 20, 10);
  ctx.fillStyle = sel ? ucCol(p) : out ? 'rgba(255,255,255,.03)' : 'rgba(255,255,255,.06)'; ctx.fill();
  if (!sel && !out) { ctx.lineWidth = 1; ctx.strokeStyle = ucCol(p); ctx.stroke(); }
  text(ucName(p), x + 10, y + 0.5, { size: 11, weight: 700, color: sel ? C.ink : out ? '#76948A' : ucCol(p) });
  const t = tag || (out ? '出局' : '');
  if (t) text(t, x + 94, y + 0.5, { size: 10, weight: 700, color: sel ? C.ink : tag ? tagCol : '#76948A', align: 'right' });
}
const ucChipXY = (i, y0, step) => [i & 1 ? 124 : 12, y0 + (i >> 1) * step];
function ucTitle() {
  text(`谁是卧底 · 第 ${G.uc.round} 轮`, W / 2, 10, { size: 9, color: C.muted, align: 'center' });
  text(`剩 ${ucAlive(false) + ucAlive(true)} 人`, 8, 10, { size: 9, color: C.muted });
}
function drawUcHandoff() {
  const p = G.handoffTo;
  drawRoom(170); drawRim(170); drawNabo(W / 2, 170, 150);
  text('请交给', W / 2, 196, { size: 12, color: C.muted, align: 'center' });
  text(ucName(p), W / 2, 222, { size: 26, weight: 700, color: ucCol(p), align: 'center' });
  text(G.uc.recheck ? '再看一眼自己的词' : `看自己的词（第 ${p + 1} / ${G.uc.n} 位）`, W / 2, 252, { size: 10, color: C.muted, align: 'center' });
  rr(48, 276, W - 96, 24, 12); ctx.fillStyle = ucCol(p); ctx.fill();
  text('准备好按 OK', W / 2, 288.5, { size: 12, weight: 700, color: C.ink, align: 'center' });
  text('别让别人看到屏幕 · ' + PAUSE_TIP, W / 2, 312, { size: 9, color: C.muted, align: 'center' });
}
function drawUc() {
  const u = G.uc;
  if (G.scene === 'ucSetup') {
    drawRoom(168); drawRim(168); drawNabo(W / 2, 168, 150);
    text('谁是卧底', W / 2, 192, { size: 22, weight: 700, color: C.brass, align: 'center' });
    text('几个人玩？', W / 2, 214, { size: 11, color: C.muted, align: 'center' });
    rr(74, 228, 56, 40, 10); ctx.fillStyle = C.brass; ctx.fill();
    text(String(u.cursor), 102, 250, { size: 26, font: F_DISP, color: C.ink, align: 'center' });
    text('人', 140, 249, { size: 16, weight: 700, color: C.card });
    text(`${u.cursor >= 7 ? 2 : 1} 个卧底`, 182, 249, { size: 11, color: C.muted });
    hint('↑↓ 人数 · OK 发词 · 长按 OK 返回');
    return;
  }
  drawRoom(92); drawRim(92); drawNabo(W / 2, 92, 72);
  if (G.scene === 'ucWord') {
    const p = G.turn;
    playerChip(Math.min(p, 3), `${ucName(p)} · 你的词`);
    rr(30, 120, 180, 76, 12); ctx.fillStyle = C.card; ctx.fill(); ctx.lineWidth = 2; ctx.strokeStyle = ucCol(p); ctx.stroke();
    text(ucWord(p), W / 2, 159, { size: 24, weight: 700, color: C.ink, align: 'center' });
    text('你可能是卧底，自己并不知道', W / 2, 220, { size: 11, color: C.muted, align: 'center' });
    text('描述时别说得太直白', W / 2, 238, { size: 11, color: C.muted, align: 'center' });
    hint(u.recheck ? '记住了按 OK 盖上' : p + 1 < u.n ? `OK 盖上，交给${ucName(p + 1)}` : 'OK 盖上，开始描述');
    return;
  }
  if (G.scene === 'ucEnd') {
    const civ = u.swap ? 1 : 0;
    text('谁是卧底 · 结果', W / 2, 10, { size: 9, color: C.muted, align: 'center' });
    text(u.ucWon ? '卧底胜利' : '平民胜利', W / 2, 113, { size: 24, weight: 700, color: u.ucWon ? C.red : C.brass, align: 'center' });
    text('平民词', 22, 145, { size: 11, color: C.muted }); text(UC_WORDS[u.pair][civ], 64, 145, { size: 22, weight: 700, color: C.card });
    text('卧底词', 22, 173, { size: 11, color: C.muted }); text(UC_WORDS[u.pair][1 - civ], 64, 173, { size: 22, weight: 700, color: C.red });
    for (let p = 0; p < u.n; p++) { const [x, y] = ucChipXY(p, 198, 22); ucChip(x, y, p, !u.alive[p], false, u.uc[p] ? '卧底' : null, C.red); }
    choices(304, ['再来一局', '回封面'], G.choice, u.ucWon ? C.red : C.brass);
    return;
  }
  ucTitle();
  if (G.scene === 'ucTalk') {
    text('轮流描述自己的词', W / 2, 113, { size: 15, weight: 700, color: C.card, align: 'center' });
    text(`从 ${ucName(u.speaker)} 开始，每人说一句`, W / 2, 135, { size: 11, color: C.muted, align: 'center' });
    for (let p = 0; p < u.n; p++) { const [x, y] = ucChipXY(p, 160, 24); ucChip(x, y, p, !u.alive[p], false, p === u.speaker ? '先' : null, C.brass); }
    choices(300, ['开始投票', '有人忘词'], u.cursor, C.brass);
  } else if (G.scene === 'ucVote') {
    const forgot = u.list === 'forgot', len = ucListLen();
    text(forgot ? '谁忘了自己的词？' : '大家商量好，投谁出局？', W / 2, 113, { size: 15, weight: 700, color: C.card, align: 'center' });
    for (let row = 0; row < len - 1; row++) { const [x, y] = ucChipXY(row, 138, 24); ucChip(x, y, ucListPlayer(row), false, row === u.cursor); }
    const last = u.cursor === len - 1, by = 138 + Math.floor(len / 2) * 24 + 6;
    rr(60, by - 11, 120, 22, 11); ctx.fillStyle = last ? C.brass : 'rgba(255,255,255,.06)'; ctx.fill();
    text(forgot ? '返回' : '平票，再说一轮', W / 2, by + 0.5, { size: 13, weight: 700, color: last ? C.ink : C.card, align: 'center' });
    if (!u.confirm) { hint(forgot ? '↑↓ 选人 · OK 交给他看词' : '↑↓ 选人 · OK 确定'); return; }
    const p = ucListPlayer(u.cursor);
    ctx.fillStyle = 'rgba(4,12,10,.92)'; ctx.fillRect(0, 266, W, 54); ctx.fillStyle = ucCol(p); ctx.fillRect(0, 266, W, 2);
    text(`投出 ${ucName(p)}？`, W / 2, 282, { size: 14, weight: 700, color: ucCol(p), align: 'center' });
    choices(304, ['确定出局', '取消'], u.confirmCursor, ucCol(p));
  } else if (G.scene === 'ucOut') {
    const p = u.out, und = ucAlive(true);
    text(`${ucName(p)} 出局`, W / 2, 129, { size: 24, weight: 700, color: ucCol(p), align: 'center' });
    text(u.uc[p] ? '是卧底！' : '是平民', W / 2, 167, { size: 24, weight: 700, color: u.uc[p] ? C.red : C.card, align: 'center' });
    if (G.result) { text(u.ucWon ? '卧底撑到了最后' : '卧底全部出局', W / 2, 215, { size: 15, weight: 700, color: C.brass, align: 'center' }); hint('OK 看结果'); }
    else { text(`还剩 ${und + ucAlive(false)} 人，卧底还有 ${und} 个`, W / 2, 211, { size: 11, color: C.muted, align: 'center' }); hint('OK 继续描述下一轮'); }
  }
}
