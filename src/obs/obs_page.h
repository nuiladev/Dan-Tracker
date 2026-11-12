#pragma once

static const char* kObsPage =
R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>Dan Tracker</title>
<style>
  :root { --bg: .92; }
  html, body { margin: 0; background: transparent !important; overflow: hidden; }
  body { font-family: "Segoe UI", sans-serif; color: #F2F2F7; -webkit-font-smoothing: antialiased; }
  .num { font-family: Bahnschrift, "Segoe UI", sans-serif; font-variant-numeric: tabular-nums; }
  #panel { width: 360px; box-sizing: border-box; padding: 14px 16px 14px; margin: 2px;
           background: rgba(20,20,28,var(--bg)); border: 1px solid rgba(44,44,60,var(--bg)); border-radius: 18px; position: relative; overflow: hidden; }
  #panel::before { content: ""; position: absolute; left: 18px; right: 18px; top: 0; height: 2px;
                   background: linear-gradient(90deg, #8A6CFF, #FF66AA); border-radius: 2px; }
  .head { display: flex; align-items: center; gap: 10px; height: 22px; }
  .pill { background: #FF66AA; color: #fff; font-size: 10.5px; font-weight: 700; border-radius: 9px; padding: 2px 9px; }
  .dan { font-size: 15px; font-weight: 700; flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .st { font-size: 10.5px; font-weight: 700; color: #8A8AA0; display: flex; align-items: center; gap: 6px; }
  .st.on { color: #6EE07A; } .dot { width: 6px; height: 6px; border-radius: 3px; background: #6EE07A; }
  .card { margin: 12px -4px 0; background: rgba(30,30,42,var(--bg)); border: 1px solid rgba(44,44,60,var(--bg)); border-radius: 14px; padding: 12px 16px 10px; }
  .row1 { display: flex; justify-content: space-between; font-size: 10.5px; font-weight: 700; }
  .tag { color: #FF66AA; } .live { color: #FF5A5A; } .rest { color: #FFD45A; }
  .song { font-size: 18px; font-weight: 700; margin-top: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .big { display: flex; justify-content: space-between; align-items: center; height: 54px; font-size: 44px; font-weight: 700; }
  .bar { height: 5px; background: #2C2C3C; border-radius: 3px; margin-top: 6px; overflow: hidden; }
  .bar > div { height: 100%; background: linear-gradient(90deg, #8A6CFF, #FF66AA); border-radius: 3px; transition: width .1s linear; }
  .judges { display: grid; grid-template-columns: repeat(6, 1fr); text-align: center; margin-top: 9px; }
  .judges b { display: block; font-size: 10.5px; } .judges span { font-size: 15px; font-weight: 600; }
  .list { margin-top: 12px; }
  .it { display: flex; flex-wrap: wrap; align-items: center; height: 44px; margin: 0 -4px 4px; padding: 0 2px 0 10px; border-radius: 9px; position: relative; align-content: center; }
  .sj { flex-basis: 100%; display: grid; grid-template-columns: repeat(6, 1fr); padding-left: 30px; font-size: 12px; font-weight: 600; margin-top: -2px; }
  .it.cur { background: rgba(255,102,170,.12); }
  .it.cur::before { content: ""; position: absolute; left: 0; top: 7px; bottom: 7px; width: 3px; border-radius: 2px; background: #FF66AA; }
  .circ { width: 20px; height: 20px; border-radius: 10px; background: #2C2C3C; color: #fff; font-size: 10.5px; font-weight: 700;
          display: flex; align-items: center; justify-content: center; margin-right: 10px; flex: none; }
  .done .circ { background: #FF66AA; } .cur .circ { background: #8A6CFF; }
  .nm { flex: 1; font-size: 13.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .up .nm, .up .ac { color: #55556A; }
  .ac { font-size: 15px; font-weight: 600; width: 80px; text-align: right; }
  .gr { font-size: 15px; font-weight: 700; width: 26px; text-align: right; }
  .total { margin: 8px -4px 0; padding: 8px 12px 6px;
           background: rgba(30,30,42,var(--bg)); border: 1px solid rgba(44,44,60,var(--bg)); border-radius: 12px; }
  .total .top { display: flex; align-items: center; justify-content: space-between; }
  .total .l { font-size: 10.5px; font-weight: 700; color: #8A8AA0; }
  .total .a { font-size: 18px; font-weight: 700; }
  .total .judges { margin-top: 4px; }
  .wait h1 { font-size: 18px; margin: 14px 0 4px; } .wait p { margin: 2px 0; font-size: 12px; color: #8A8AA0; }
  body.thin { text-shadow: 0 1px 3px rgba(0,0,0,.9), 0 0 2px rgba(0,0,0,.9); }
  body.thin .bar { background: rgba(44,44,60,.6); }
  body.thin .up .nm, body.thin .up .ac, body.thin .total .l { color: #D0D0DC !important; }
  .note { margin-top: 6px; font-size: 10.5px; font-weight: 700; color: #FFD45A; }
</style></head>
<body><div id="panel"></div>
)HTML"
R"HTML(<script>
const Q = new URLSearchParams(location.search);
if (Q.has("bg")) {
  const bg = Math.min(1, Math.max(0, parseFloat(Q.get("bg")) || 0));
  document.documentElement.style.setProperty("--bg", bg);
  if (bg < 0.5) document.body.classList.add("thin");
}
const C = { max: "#BFF4FF", h300: "#FFD45A", h200: "#6EE07A", h100: "#4FA8FF", h50: "#A8A8B8", miss: "#FF5A5A", faint: "#55556A" };
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const fmt = (a, n) => n ? a.toFixed(2) + "%" : "--.--%";
function grade(a, n) {
  if (!n) return ["-", C.faint];
  if (a >= 100) return ["SS", "#FFF4C2"]; if (a > 95) return ["S", C.h300]; if (a > 90) return ["A", C.h200];
  if (a > 80) return ["B", C.h100]; if (a > 70) return ["C", "#C27BFF"]; return ["D", C.miss];
}
function render(d) {
  const p = document.getElementById("panel");
  if (!d.connected || !d.isDan || !d.songs.length) {
    p.innerHTML = `<div class="head"><span class="pill">DAN</span><span class="dan">Dan Tracker</span></div>
      <div class="wait"><h1>${d.connected ? "No dan selected" : "Waiting for tosu..."}</h1>
      <p>${esc(d.mapLabel || "")}</p><p>${esc(d.note || "")}</p></div>`;
    return;
  }
  const n = d.songs.length, cur = Math.min(Math.max(d.current, 0), n - 1), s = d.songs[cur];
  let right = "";
  if (d.playing && !d.finished) {
    let next = -1;
    if (d.liveMs > s.endMs && cur + 1 < n) next = d.songs[cur + 1].startMs;
    else if (d.liveMs < s.startMs && cur > 0) next = s.startMs;
    right = next >= 0 ? `<span class="rest">BREAK&nbsp; ${Math.max(0, (next - d.liveMs) / 1000).toFixed(1)}s</span>`
                      : `<span class="live">&#9679; LIVE</span>`;
  }
  let prog = s.endMs > s.startMs ? Math.min(1, Math.max(0, (d.liveMs - s.startMs) / (s.endMs - s.startMs))) : 0;
  if (d.finished || s.status === "done") prog = 1;
  const [g, gc] = grade(s.acc, s.judged);
  const judges = h => [["MAX", h.max, C.max], ["300", h.h300, C.h300], ["200", h.h200, C.h200], ["100", h.h100, C.h100], ["50", h.h50, C.h50], ["MISS", h.miss, C.miss]];
  const labelled = h => judges(h).map(([l, v, c]) => `<div><b style="color:${c}">${l}</b><span class="num" style="color:${v ? "#F2F2F7" : C.faint}">${v}</span></div>`).join("");
  const st = d.finished ? "COMPLETE" : d.playing ? "PLAYING" : "IDLE";
  let html = `<div class="head"><span class="pill">DAN</span><span class="dan">${esc(d.danName)}</span>
      <span class="st ${d.playing ? "on" : ""}">${st}<span class="dot"></span></span></div>
    <div class="card"><div class="row1"><span class="tag">SONG ${cur + 1} / ${n}</span>${right}</div>
      <div class="song">${esc(s.name)}</div>
      <div class="big"><span class="num">${fmt(s.acc, s.judged)}</span><span class="num" style="color:${gc}">${g}</span></div>
      <div class="bar"><div style="width:${(prog * 100).toFixed(1)}%"></div></div>
      <div class="judges">${labelled(s)}</div>
    </div><div class="list">`;
  d.songs.forEach((x, i) => {
    const live = i === cur && !d.finished && x.status !== "done";
    const cls = x.status === "done" ? "done" : live ? "cur" : "up";
    const [gg, ggc] = grade(x.acc, x.judged);
    const acc = cls === "up" ? "&mdash;" : fmt(x.acc, x.judged);
    html += `<div class="it ${cls}"><span class="circ">${i + 1}</span><span class="nm">${esc(x.name)}</span>
      <span class="ac num">${acc}</span><span class="gr" style="color:${ggc}">${cls === "up" ? "" : gg}</span>
      ${cls === "up" ? "" : `<div class="sj num">${judges(x).map(([, v, c]) => `<span style="color:${c};opacity:${v ? 1 : .35}">${v}</span>`).join("")}</div>`}</div>`;
  });
  const t = d.total;
  html += `</div><div class="total"><div class="top"><span class="l">TOTAL DAN</span>
    <span class="a num">${fmt(t.acc, t.judged)}</span></div><div class="judges">${labelled(t)}</div></div>`;
  if (d.note) html += `<div class="note">${esc(d.note)}</div>`;
  p.innerHTML = html;
}
async function tick() {
  try { render(await (await fetch("/state", { cache: "no-store" })).json()); }
  catch (e) { render({ connected: false, songs: [], note: "Open DanTracker.exe" }); }
  setTimeout(tick, 100);
}
tick();
</script></body></html>
)HTML";
