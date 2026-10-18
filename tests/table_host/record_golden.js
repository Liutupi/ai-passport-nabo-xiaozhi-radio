// Run inside the browser prototype (served by golden_server.py) to record golden traces.
// Each trace: a config, a seed, the exact key presses / frame advances, and a state snapshot
// after every step. test_table.py replays the same steps on the C model and compares.
const SCENES = ['menu', 'handoff', 'bjDeal', 'bjTurn', 'bjReveal', 'diceRoll', 'diceTurn', 'diceReveal', 'end'];
function snap() {
  const s = {
    sc: SCENES.indexOf(G.scene), g: G.game === 'bj' ? 1 : G.game === 'dice' ? 2 : 0, score: [...G.score],
    round: G.round, starter: G.starter, turn: G.turn, ho: G.handoffTo, pause: G.pause ? G.pause.cursor : -1,
    choice: G.choice, menu: G.menu, target: Cfg.target,
    w: G.result ? (G.result.winner === null ? -1 : G.result.winner) : -2,
  };
  if (G.game === 'bj' && G.bj) s.bj = {
    h: G.bj.hands.map(h => h.map(c => c.r * 10 + c.s)), st: G.bj.stood.map(Number),
    ph: G.bj.phase === 'hitShow' ? 1 : 0, cu: G.bj.cursor, d: G.bj.dealt, n: G.bj.deck.length,
  };
  if (G.game === 'dice' && G.dc) s.dc = {
    d: G.dc.dice, y: G.dc.yaw.map(a => a.map(v => Math.round(v * 1000))), b: G.dc.bids.map(b => [b.p, b.q, b.f]),
    st: G.dc.step === 'face' ? 1 : 0, q: G.dc.qList.map(v => (v === 'open' ? 0 : v)), qi: G.dc.qi,
    f: G.dc.fList.map(v => (v === 'back' ? 0 : v)), fi: G.dc.fi, ca: G.dc.caller,
  };
  return s;
}
async function recordTrace(name, cfg, seed, nSteps) {
  Object.assign(Cfg, cfg);
  // Fresh state, as the firmware starts it: nothing carries over from the previous trace.
  Object.assign(G, { score: [0, 0], round: 0, starter: 0, loser: 0, turn: 0, handoffTo: 0, result: null, bj: null, dc: null });
  rngState = seed >>> 0; G.pause = null; G.menu = 0; G.choice = 0; toMenu();
  let pol = (seed * 7919 + 13) >>> 0;   // the policy has its own generator, so replays are exact
  const r = () => ((pol = (pol * 1103515245 + 12345) >>> 0) >>> 8) / 16777216;
  const steps = [], snaps = [];
  const key = k => { steps.push({ a: 'k', k }); act(k); snaps.push(snap()); };
  const frames = n => { steps.push({ a: 'f', n }); for (let i = 0; i < n; i++) update(1 / 60); snaps.push(snap()); };
  const long = () => { steps.push({ a: 'l' }); if (canPause()) G.pause = { cursor: 0 }; snaps.push(snap()); };
  while (steps.length < nSteps) {
    if (steps.length % 23 === 22 && canPause()) { long(); continue; }
    if (G.pause) { key(r() < 0.3 ? 'down' : 'ok'); continue; }
    switch (G.scene) {
      case 'menu': key(r() < 0.25 ? 'down' : r() < 0.15 ? 'up' : 'ok'); break;
      case 'handoff': key('ok'); break;
      case 'bjDeal': case 'diceRoll': frames(30); break;
      case 'bjTurn': key(G.bj.phase === 'choose' && r() < 0.35 ? 'down' : 'ok'); break;
      case 'diceTurn': { const x = r(); key(x < 0.3 ? 'up' : x < 0.5 ? 'down' : 'ok'); break; }
      case 'bjReveal': case 'diceReveal':
        if (!G.result) frames(30); else key(r() < 0.15 ? 'down' : 'ok');
        break;
      case 'end': key(r() < 0.3 ? 'down' : 'ok'); break;
    }
  }
  const body = JSON.stringify({ cfg, seed, steps, snaps });
  await fetch(`/save/${name}.json`, { method: 'POST', body });
  return { name, steps: steps.length, bytes: body.length };
}
