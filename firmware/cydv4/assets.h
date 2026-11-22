#pragma once
#include <Arduino.h>

// Сгенерировано из web/app.css, web/app.js и web/history.js - НЕ ПРАВИТЬ ВРУЧНУЮ
// (python web/build_page.py).
static const char APP_CSS[] PROGMEM = R"CSS(
:root{
 --bg:#0a0f1a; --card:rgba(255,255,255,.035); --card2:rgba(255,255,255,.012); --ln:#1e293b;
 --ink:#e8eefc; --mut:#7f8ea8; --dim:#5f708a;
 --c1:#22d3ee; --c2:#34d399; --c3:#a78bfa; --c4:#fbbf24; --c5:#f472b6; --red:#f87171;
 --bar1:#22d3ee; --bar2:#fbbf24; --grid:#1e293b;
}
html[data-theme="light"]{
 --bg:#f4f6fb; --card:#fff; --card2:#fff; --ln:#e6ecf5;
 --ink:#0f172a; --mut:#64748b; --dim:#7c8ba1;
 --c1:#2563eb; --c2:#059669; --c3:#7c3aed; --c4:#c2410c; --c5:#be185d; --red:#dc2626;
 --bar1:#93b4f8; --bar2:#f59e0b; --grid:#e6ecf5;
}
*{box-sizing:border-box}
.history-workspace{margin:24px 0;scroll-margin-top:20px}.history-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.history-metric{padding:18px;background:var(--card2);border:1px solid var(--ln);border-radius:16px}.history-metric svg{width:100%;display:block}.history-value{font-size:28px;font-weight:700;color:var(--c1);margin:10px 0}.history-metric [role=button]{cursor:pointer}.history-metric [role=button]:focus{outline:2px solid var(--c4)}#historySummary{padding:12px 0;font-weight:600}@media(max-width:750px){.history-grid{grid-template-columns:1fr}}
.history-combined{background:var(--card2);padding:16px;border:1px solid var(--ln);border-radius:16px}.history-combined svg{width:100%;display:block}.history-combined [data-hour]{cursor:pointer}.history-legend{display:flex;gap:12px;flex-wrap:wrap;color:var(--mut);font-size:12px}.history-legend i{display:inline-block;width:9px;height:9px;margin-right:5px;border-radius:2px}.history-grid{grid-template-columns:repeat(auto-fit,minmax(155px,1fr));margin-top:12px}.history-grid .history-metric{padding:12px}.history-grid .history-metric svg{display:none}.history-grid .history-value{font-size:20px}
.history-legend button{border:1px solid var(--ln);border-radius:7px;background:transparent;color:inherit;padding:5px 8px;cursor:pointer;font:inherit}.history-legend button[aria-pressed=false]{opacity:.42;text-decoration:line-through}.history-legend button:focus-visible{outline:2px solid var(--c4)}.history-combined [data-index]{cursor:pointer}
#rngControls{margin:10px 0 0;padding:10px 12px;background:var(--card2);border:1px solid var(--ln);border-radius:14px}
#rngControls .qrow{margin:0}
#rngControls #gapInfo{font-weight:600;color:var(--ink)}
#rngControls #rngNote{color:var(--mut)}
.history-workspace #historySummary:empty{display:none}
#historyWorkspace .history-combined svg[aria-busy=true]{display:none}
/* один свёрнутый блок истории: summary-строка и тело */
.history-workspace>summary{cursor:pointer;list-style:none;display:flex;align-items:baseline;gap:10px;flex-wrap:wrap;font-weight:800;font-size:15px;letter-spacing:-.01em;padding:2px 0;user-select:none}
.history-workspace>summary::-webkit-details-marker{display:none}
.history-workspace>summary::marker{content:""}
.history-workspace>summary:before{content:"\25B8";color:var(--c1);font-size:13px;line-height:1;display:inline-block;transition:transform .15s}
.history-workspace[open]>summary:before{transform:rotate(90deg)}
.history-workspace>summary:hover{color:var(--c1)}
.history-workspace .hs-hint{margin-left:auto;font-size:11.5px;font-weight:600;color:var(--mut);letter-spacing:0;text-transform:none}
.history-workspace[open] .hs-hint{display:none}
.history-body{padding-top:14px}
.history-workspace #historySummary{color:var(--mut)}
.history-workspace.hours-mode .period-only{display:none}
@media(max-width:600px){.history-workspace .hs-hint{display:none}}
@media(max-width:600px){#rngControls{padding:8px 10px}}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;min-height:100vh;transition:background .15s,color .15s}
.wrap{max-width:1000px;margin:0 auto;padding:20px 16px 44px}
.top{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:14px}
.name{font-size:18px;font-weight:800;letter-spacing:-.02em}
.name i{color:var(--c1);font-style:normal}
.ver{font-size:10.5px;font-weight:700;color:var(--c1);background:transparent;border:1px solid var(--c1);border-radius:999px;padding:2px 8px;opacity:.95}
.chips{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap}
.chip{display:inline-flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--ln);
border-radius:999px;padding:4px 10px;font-size:12px;color:var(--mut)}
.dot{width:7px;height:7px;border-radius:50%;background:#475569}
.dot.ok{background:var(--c2)}
.iconbtn{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:999px;
padding:5px 11px;font:inherit;font-size:12px;cursor:pointer}
.iconbtn:hover{border-color:var(--c1)}
.hero{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;align-items:stretch}
@media(max-width:780px){.hero{grid-template-columns:repeat(2,minmax(0,1fr))}}
.card{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid var(--ln);border-radius:18px;padding:14px 16px}






.cap{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--mut);font-weight:700}
.pbar{height:9px;border-radius:99px;background:var(--grid);overflow:hidden;margin-top:10px}.pbar i{display:block;height:100%;width:0;background:var(--c1);border-radius:99px;transition:width .25s ease}
.tiles{display:grid;grid-template-columns:1fr;gap:10px;align-content:stretch}
@media(max-width:520px){.tiles{grid-template-columns:repeat(2,minmax(0,1fr))}}
.t{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid var(--ln);border-radius:14px;padding:11px 12px}
.t .k{font-size:9.5px;text-transform:uppercase;letter-spacing:.09em;color:var(--mut);font-weight:700}
.t .v{font-size:21px;font-weight:800;letter-spacing:-.03em;margin-top:3px;line-height:1.15}
.t .v small{font-size:11px;color:var(--mut);font-weight:600;margin-left:3px}
.t .s{font-size:10px;color:var(--dim);margin-top:2px}
.c1{color:var(--c1)}.c2{color:var(--c2)}.c3{color:var(--c3)}.c4{color:var(--c4)}.c5{color:var(--c5)}
.row4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:12px}
@media(max-width:780px){.row4{grid-template-columns:repeat(2,minmax(0,1fr))}}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}
@media(max-width:780px){.two{grid-template-columns:1fr}}
.head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:8px}
.ttl{font-size:10.5px;font-weight:800;text-transform:uppercase;letter-spacing:.09em;color:var(--mut)}
.legend{display:flex;gap:12px;font-size:10.5px;color:var(--mut);margin-left:auto}
.sw{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:4px}
.kv{display:flex;justify-content:space-between;gap:12px;font-size:13px;padding:7px 0;border-top:1px solid var(--ln)}
.kv:first-of-type{border-top:0}
svg.chart{display:block;width:100%;height:auto;touch-action:manipulation}
.ax{font-size:9px;fill:var(--dim)}
.nav{display:flex;align-items:center;gap:8px;margin-left:auto}
.nav button,.ctrl button{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:9px;
padding:4px 9px;cursor:pointer;font:inherit;font-size:12px}
.nav button:disabled{opacity:.35;cursor:default}
.ctrl{display:flex;gap:6px;margin-left:auto}
.ctrl button.on{border-color:var(--c1);color:var(--c1);font-weight:700;background:var(--card)}
.date{font-size:12px;color:var(--mut);text-align:center;min-width:110px}
.gapbox{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--c4);margin-top:8px}
input[type=range]{width:120px;accent-color:var(--c1)}
input[type=number]{width:56px;background:var(--card);border:1px solid var(--ln);color:var(--ink);
border-radius:8px;padding:2px 6px;font:inherit;text-align:right}
.tip{position:fixed;z-index:50;background:var(--bg);color:var(--ink);border:1px solid var(--ln);border-radius:10px;
padding:8px 10px;font-size:12px;box-shadow:0 8px 24px rgba(0,0,0,.35);pointer-events:none;max-width:250px;display:none}

.readout{display:flex;flex-wrap:wrap;gap:6px 16px;align-items:baseline;font-size:12.5px;color:var(--mut);
background:var(--card);border:1px solid var(--ln);border-radius:12px;padding:7px 11px;margin:0 0 8px;min-height:34px}
.readout b{color:var(--ink);font-weight:700}
.readout .h{color:var(--c1);font-weight:800}
.readout .empty{opacity:.6}
svg.chart rect.bar,svg.chart rect.bar2{cursor:pointer}svg.chart rect.hit,svg.chart rect.hit2{cursor:pointer}
svg.chart rect.bar.sel{stroke:var(--ink);stroke-width:2.5}
svg.chart rect.bar2.sel{stroke:var(--c1);stroke-width:2}
svg.chart{cursor:default}

.detail{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-bottom:12px}
@media(max-width:900px){.detail{grid-template-columns:1fr}}
.d .bigv{font-size:30px;font-weight:800;letter-spacing:-.03em;line-height:1.1}
.d .bigv small{font-size:12px;color:var(--mut);font-weight:600;margin-left:4px}
.d .dsub{font-size:10.5px;color:var(--dim);margin-top:2px}
.d .dnow{font-size:10.5px;color:var(--mut);margin-left:auto}
.d .dstats{display:flex;gap:14px;margin-top:8px;font-size:11px;color:var(--mut)}
.d .dstats b{display:block;color:var(--ink);font-size:13px;font-weight:700}
.d .dstats i{font-style:normal;font-size:9.5px;color:var(--dim)}
.d svg.dchart{display:block;width:100%;height:64px;margin-top:8px}
.build{font-size:10px;color:var(--dim);text-align:center;margin-top:14px;opacity:.7}

.qgrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}@media(max-width:820px){.qgrid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:520px){.qgrid{grid-template-columns:1fr}}.qrow{display:flex;gap:10px;align-items:center;flex-wrap:wrap;font-size:12.5px;color:var(--mut);padding:6px 0}.qrow b{color:var(--ink)}.qbig{font-size:26px;font-weight:800;letter-spacing:-.03em;line-height:1.1}.qbig small{font-size:12px;color:var(--mut);font-weight:600;margin-left:3px}.pbar2{height:8px;border-radius:99px;background:var(--grid);overflow:hidden;margin-top:6px}.pbar2 i{display:block;height:100%;width:0;background:var(--c1);border-radius:99px}.qbtn{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:9px;padding:4px 10px;cursor:pointer;font:inherit;font-size:12px}.qbtn.on{border-color:var(--c2);color:var(--c2);font-weight:700}.qbtn.danger.on{border-color:var(--red);color:var(--red)}.qin{width:64px;background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:8px;padding:3px 7px;font:inherit;font-size:12px;text-align:right}.evlist{max-height:190px;overflow:auto;font-size:12px;color:var(--mut)}.ev{border-bottom:1px solid var(--ln);padding:5px 0;display:flex;gap:8px}.ev:last-child{border-bottom:0}.ev .t{color:var(--ink);font-variant-numeric:tabular-nums}.ev .sag{color:var(--c4)} .ev .swl{color:var(--red)}
.daybar{display:flex;align-items:center;gap:8px;flex-wrap:wrap;position:sticky;top:0;z-index:20;
 background:color-mix(in srgb,var(--bg) 92%,transparent);backdrop-filter:blur(6px);
 border:1px solid var(--ln);border-radius:14px;
 padding:8px 10px;margin:0 0 12px}
/* деньбар теперь берёт цвет фона скина */
.daybar .lbl{font-size:13px;font-weight:700}
.daybar .note{font-size:11.5px;color:var(--mut);margin-left:auto}
.sect{font-size:11px;text-transform:uppercase;letter-spacing:.12em;color:var(--mut);
 font-weight:800;margin:20px 0 8px;display:flex;align-items:center;gap:8px}
.sect:before{content:"";width:14px;height:2px;background:var(--c1);border-radius:2px}
.sect span{font-weight:600;letter-spacing:0;text-transform:none;color:var(--dim);font-size:11px}
#printMeta{display:none}
.pb{display:flex;gap:6px;flex-wrap:wrap;margin-top:2px}
.pb button{background:var(--card);border:1px solid var(--ln);color:var(--mut);border-radius:999px;
 padding:5px 11px;font:inherit;font-size:12px;cursor:pointer;white-space:nowrap}
.pb button:hover{border-color:var(--c1);color:var(--ink)}
.pb button.on{background:var(--c1);border-color:var(--c1);color:#04202a;font-weight:700}
.rt{width:100%;border-collapse:collapse;margin-top:10px;font-size:12.5px}
.rt th{font-size:10px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);font-weight:700;
 text-align:right;padding:6px 4px;border-bottom:1px solid var(--ln);white-space:nowrap}
.rt th:first-child,.rt td:first-child{text-align:left}
.rt td{padding:6px 4px;border-bottom:1px solid var(--ln);text-align:right;color:var(--ink);white-space:nowrap}
.rt tr:hover td{background:var(--card)}
.rt td.mut{color:var(--mut)}
.rtwrap{overflow-x:auto}
@media print{
  @page{size:A4 portrait;margin:12mm}
  body{background:#fff;color:#000}
  .wrap{max-width:none;padding:0}
  .wrap>*{display:none!important}
  .wrap>#historyWorkspace{display:block!important;background:#fff!important;color:#000!important;border:0!important;border-radius:0!important;padding:0;margin:0;box-shadow:none!important}
  #historyWorkspace>summary{display:none!important}
  #historyWorkspace .history-body{padding:0}
  #historyWorkspace .history-body>.head,#historyWorkspace .history-body>.mut,
  #historyWorkspace #historyModes,#historyWorkspace .pb,
  #historyWorkspace #historyDrill,#historyWorkspace #historyMetrics,
  #historyWorkspace #historySummary,#historyWorkspace #rngControls,
  #historyWorkspace .history-combined>p{display:none!important}
  /* P2.4: легенду серий печатаем — иначе в чёрно-белой печати цвета без подписей */
  #historyWorkspace .history-legend{display:flex!important;gap:10px;flex-wrap:wrap;color:#000!important;font-size:9pt;margin:0 0 6px}
  #historyWorkspace .history-legend button{border:1px solid #888!important;background:#fff!important;color:#000!important;padding:2px 6px}
  #historyWorkspace .history-legend button[aria-pressed=false]{opacity:1!important;text-decoration:none!important}
  #historyWorkspace .history-legend i{border:1px solid #666}
  /* P2.1–P2.4 — экранные свёрнутые блоки: на бумаге их подписи без фона не читаются */
  #historyWorkspace .p2box{display:none!important}
  #printMeta{display:block;font-size:8.5pt;color:#333;margin:0 0 8px}
  #historyWorkspace .history-combined{background:#fff!important;border:1px solid #999;border-radius:4px;padding:8px;box-shadow:none!important}
  #historyWorkspace .history-combined .ccap{font-size:12pt;font-weight:700;color:#000;margin-bottom:6px}
  #historyWorkspace .row4{grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}
  #historyWorkspace .t{background:#fff!important;border:1px solid #999;border-radius:4px;padding:5px 7px}
  #historyWorkspace .t .k{color:#444}
  #historyWorkspace .t .v{color:#000!important}
  #historyWorkspace .t .s{color:#555}
  .rt{font-size:9pt}
  .rt th{color:#222!important;border:1px solid #888!important;padding:4px 5px}
  .rt td{color:#000!important;border:1px solid #bbb!important;padding:3px 5px}
  .rt tr{break-inside:avoid}
  #tip{display:none}
}
/* ================= скины — те же шесть, что на плате ================= */
html[data-skin="0"]{ /* светлая */
 --bg:#f8faff; --card:#ffffff; --card2:#eff4ff; --ln:#dbe4f5;
 --ink:#0f172a; --mut:#64748b; --dim:#7c8ba1;
 --c1:#2563eb; --c2:#10b981; --c3:#8b5cf6; --c4:#f59e0b; --c5:#db2777; --red:#ef4444;
 --bar1:#2563eb; --bar2:#8b5cf6; --grid:#e6ecf5;
}
html[data-skin="1"]{ /* тёмная */
 --bg:#0b111b; --card:#141c2a; --card2:#1c2636; --ln:#243044;
 --ink:#e8eefc; --mut:#93a4be; --dim:#6b7c96;
 --c1:#22d3ee; --c2:#34d399; --c3:#fbbf24; --c4:#34d399; --c5:#f472b6; --red:#f87171;
 --bar1:#22d3ee; --bar2:#fbbf24; --grid:#243044;
}
html[data-skin="2"]{ /* ЧБ */
 --bg:#000000; --card:#1a1a1a; --card2:#282828; --ln:#464646;
 --ink:#ffffff; --mut:#a0a0a0; --dim:#808080;
 --c1:#ffffff; --c2:#ffffff; --c3:#ffffff; --c4:#cdcdcd; --c5:#ffffff; --red:#ffffff;
 --bar1:#ffffff; --bar2:#cdcdcd; --grid:#464646;
}
html[data-skin="3"]{ /* розовая */
 --bg:#ffe0ec; --card:#fff6ff; --card2:#ffcde1; --ln:#f7b8d2;
 --ink:#3c1428; --mut:#965a78; --dim:#b07f99;
 --c1:#ec4899; --c2:#10b981; --c3:#db2777; --c4:#a855f7; --c5:#be185d; --red:#ef4444;
 --bar1:#ec4899; --bar2:#db2777; --grid:#ffd9e8;
}
html[data-skin="4"]{ /* красная */
 --bg:#2e0808; --card:#3e0e0e; --card2:#541616; --ln:#7a2c2c;
 --ink:#ffe6e6; --mut:#c89696; --dim:#a87878;
 --c1:#ef4444; --c2:#4ade80; --c3:#fb923c; --c4:#facc15; --c5:#f87171; --red:#ff0000;
 --bar1:#ef4444; --bar2:#fb923c; --grid:#501e1e;
}
html[data-skin="5"]{ /* радужная */
 --bg:#0c0a18; --card:#18122a; --card2:#241a3c; --ln:#3c2d64;
 --ink:#f0f0ff; --mut:#aaa0d2; --dim:#8a80ae;
 --c1:#00e5ff; --c2:#00ff96; --c3:#ff00c8; --c4:#aaff00; --c5:#ffe600; --red:#ff3c3c;
 --bar1:#00e5ff; --bar2:#ff00c8; --grid:#3c2d64;
}
/* скелетон загрузки */
.skel{position:relative;overflow:hidden}
.skel::after{content:"";position:absolute;inset:0;background:linear-gradient(90deg,transparent,rgba(255,255,255,.06),transparent);
 animation:sh 1.2s infinite}
@keyframes sh{0%{transform:translateX(-100%)}100%{transform:translateX(100%)}}
html[data-loading="1"] .card .val,html[data-loading="1"] .card .val2{opacity:.35}
/* плашка «нет связи» */
.netbar{display:none;align-items:center;gap:10px;margin:0 0 12px;padding:9px 12px;border-radius:12px;
 background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.35);color:var(--ink);font-size:12.5px}
.netbar.on{display:flex}
.netbar b{color:var(--red)}
.netbar button{margin-left:auto;background:var(--card);border:1px solid var(--ln);color:var(--ink);
 border-radius:9px;padding:4px 10px;font:inherit;font-size:12px;cursor:pointer}

/* кнопки в деньбаре подстраиваются под скин */
.daybar .nav button{background:color-mix(in srgb,var(--card) 80%,var(--bg));color:var(--ink);border-color:var(--ln)}
.daybar .nav button:hover{border-color:var(--c1)}

/* блок «Качество и защита» — ровно две колонки и одинаковая высота */
.qgrid.two{grid-template-columns:repeat(2,minmax(0,1fr))}
@media(max-width:780px){.qgrid.two{grid-template-columns:1fr}}
.qgrid>.card{height:100%}

/* графики периода */
.rngchart{margin-top:12px;padding:12px 14px;background:linear-gradient(180deg,var(--card),var(--card2));
 border:1px solid var(--ln);border-radius:14px}
.rngchart .ccap{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);
 font-weight:700;margin:6px 0 4px}
.svgbox svg{display:block;width:100%;height:auto}
.svgbox .bl{font:10px system-ui;fill:var(--mut)}
.svgbox .bv{font:10px system-ui;fill:var(--ink);font-weight:600}
/* Studio: shared surfaces retain all six skin palettes. */
body{background:radial-gradient(ellipse at 10% 0%,color-mix(in srgb,var(--c1) 9%,transparent),transparent 55%),var(--bg);font-variant-numeric:tabular-nums}
.wrap{max-width:1240px;padding:28px 24px 56px}
.top{padding:14px 0 22px;border-bottom:1px solid var(--ln);margin-bottom:22px}
.name{font-size:24px;letter-spacing:-.04em}
.card,.rngchart{box-shadow:0 12px 32px rgba(0,0,0,.09);border-radius:20px;padding:20px}
.sect{margin-top:26px;letter-spacing:.12em}
.d .bigv{font-size:34px}.d svg.dchart{height:84px}
.rngchart .ccap{font-size:13px;letter-spacing:.025em;text-transform:none;margin-bottom:12px;color:var(--ink)}
button,input,select{min-height:36px}
button{transition:background .15s,border-color .15s,box-shadow .15s}
button:hover,button:focus-visible{border-color:var(--c1);box-shadow:0 0 0 3px color-mix(in srgb,var(--c1) 15%,transparent)}
button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--c1);outline-offset:3px}
.svgbox g.bclick{cursor:pointer}.svgbox g.bclick:hover .bar-value,.svgbox g.bclick.selected .bar-value{stroke:var(--ink);stroke-width:2;filter:brightness(1.15)}
.bar-detail{margin:10px 0;padding:12px 14px;border:1px solid var(--ln);border-radius:12px;color:var(--ink);background:var(--card2);font-weight:600}
html[data-skin="1"]{--bg:#0b1422;--card:#142235;--card2:#101c2c;--ln:#293d54;--c3:#f472b6;--c4:#fbbf24;--bar2:#fbbf24}
html[data-skin="5"]{--bg:#101125;--card:#191e36;--card2:#14192e;--ln:#343e60;--ink:#f0f3ff;--mut:#afb9d6;--c1:#67e8f9;--c3:#f49ac2;--c4:#fbbf24;--bar1:#67e8f9;--bar2:#fbbf24;--grid:#343e60}
@media(max-width:600px){.wrap{padding:14px 12px 32px}.card,.rngchart{padding:14px;border-radius:16px}.name{font-size:21px}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
/* P0.4: неполные суммы помечаем, пустые ячейки остаются видимыми и фокусируемыми */
.hist-note{margin:12px 0 0;padding:10px 12px;border:1px solid color-mix(in srgb,var(--c4) 55%,var(--ln));border-radius:12px;background:color-mix(in srgb,var(--c4) 10%,transparent);font-size:12.5px;line-height:1.45;color:var(--c4)}
.history-grid .hist-note{grid-column:1/-1;margin:0}
.history-metric.partial{border-color:color-mix(in srgb,var(--c4) 45%,var(--ln))}
.history-metric.partial .history-value{color:var(--c4)}
.history-combined [data-index]{cursor:pointer;outline:none}
.history-combined [data-index]:focus-visible{outline:2px solid var(--c1);outline-offset:2px;border-radius:2px}
.history-combined [data-index]:hover{filter:brightness(1.12)}
/* P1.2: компактная строка состояния + панель «Состояние и качество данных».
   Цвета только из переменных скинов; разметка — только новые id, шапка не тронута. */
.statbox{margin:0 0 12px;border:1px solid var(--ln);border-radius:14px;background:var(--card)}
.statbox>summary{display:flex;align-items:center;gap:6px 14px;flex-wrap:wrap;padding:8px 12px;cursor:pointer;list-style:none;user-select:none;font-size:11.5px;line-height:1.4}
.statbox>summary::-webkit-details-marker{display:none}
.statbox>summary::marker{content:""}
.statbox>summary:hover{color:var(--c1)}
.statbox .sb-live{color:var(--ink);font-weight:600}
.statbox .sb-link{color:var(--mut)}
.statbox .sb-hint{margin-left:auto;color:var(--dim);border:1px solid var(--ln);border-radius:999px;padding:1px 8px;font-size:10.5px;white-space:nowrap}
.statbox[open] .sb-hint{border-color:var(--c1);color:var(--c1)}
.statbox .sb-body{padding:2px 12px 12px;border-top:1px solid var(--ln)}
.sb-cols{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:10px}
@media(max-width:680px){.sb-cols{grid-template-columns:1fr}}
.sb-card{background:var(--card2);border:1px solid var(--ln);border-radius:12px;padding:8px 10px}
.sb-head{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);margin-bottom:4px}
.sb-head span{float:right;font-weight:600;letter-spacing:0;text-transform:none;color:var(--dim)}
.sb-row{display:flex;justify-content:space-between;gap:10px;padding:4px 0;border-top:1px solid var(--ln);font-size:11.5px}
.sb-row:first-of-type{border-top:0}
.sb-row>span{color:var(--mut)}
.sb-row>b{font-weight:600;color:var(--ink);text-align:right;max-width:62%}
.sb-row>b.bad{color:var(--c4)}
.sb-row>b.err{color:var(--red)}
.sb-fresh{margin-top:10px}
.sb-tblwrap{overflow-x:auto}
.sb-tbl{width:100%;border-collapse:collapse;font-size:11.5px;min-width:600px}
.sb-tbl th{font-size:9.5px;text-transform:uppercase;letter-spacing:.07em;color:var(--mut);font-weight:700;text-align:left;padding:5px 6px;border-bottom:1px solid var(--ln);white-space:nowrap}
.sb-tbl td{padding:5px 6px;border-bottom:1px solid var(--ln);color:var(--ink);white-space:nowrap}
.sb-tbl tr:last-child td{border-bottom:0}
.sb-tbl td.mut{color:var(--mut)}
.sb-tbl .src-bad{color:var(--c4)}
.bigv span.none{color:var(--mut);font-weight:600}

/* P1.3/P1.5: события, диагностика, предпросмотр обезличенного отчёта */
.ev-state{display:flex;flex-wrap:wrap;gap:4px 16px;font-size:11.5px;color:var(--mut);padding:6px 0;border-bottom:1px solid var(--ln)}
.ev-state b{color:var(--ink);font-weight:600}
.ev-state i{color:var(--dim);font-style:normal}
.ev-hist{max-height:320px}
.ev-tag{min-width:54px;font-size:10.5px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut)}
.ev-tag.sag{color:var(--c4)}
.ev-tag.swl,.ev-tag.bad{color:var(--red)}
.ev-tag.reb{color:var(--c1)}
.ev-foot{font-size:10.5px;line-height:1.45;margin-top:6px}
.diag-cols{grid-template-columns:repeat(3,minmax(0,1fr))}
@media(max-width:900px){.diag-cols{grid-template-columns:1fr}}
.diag-clog{margin-top:2px;font:11px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap;color:var(--ink);background:var(--card);border:1px solid var(--ln);border-radius:8px;padding:6px 8px;max-height:110px;overflow:auto}
.diag-actions{justify-content:flex-start;border-top:1px solid var(--ln);margin-top:10px}
.rep-prev{margin-top:8px;border:1px solid var(--ln);border-radius:10px;padding:8px}
.rep-prev pre{margin:0 0 8px;max-height:260px;overflow:auto;white-space:pre-wrap;font:11px/1.5 ui-monospace,Consolas,monospace;color:var(--ink)}
.rep-prev .qbtn.on{border-color:var(--c2);color:var(--c2);font-weight:700}

/* P2.1/P2.2: сравнение периодов и пики нагрузки — свёрнутые блоки под таблицей */
.history-body .p2box{margin:12px 0 0}
.p2row{display:flex;align-items:baseline;gap:8px 12px;flex-wrap:wrap;font-size:12px;padding:5px 0;border-bottom:1px solid var(--ln)}
.p2row:last-of-type{border-bottom:0}
.p2row>span:first-child{color:var(--mut);min-width:160px}
.p2row>b{color:var(--ink);font-weight:600;font-variant-numeric:tabular-nums}
.p2row .p2m{color:var(--mut);font-variant-numeric:tabular-nums}
.p2pk{display:flex;align-items:baseline;gap:10px;width:100%;background:var(--card2);border:1px solid var(--ln);border-radius:10px;color:var(--ink);font:inherit;font-size:12px;text-align:left;padding:6px 10px;margin:4px 0;cursor:pointer}
.p2pk:hover,.p2pk:focus-visible{border-color:var(--c1);outline:none}
.p2pk span{color:var(--mut);min-width:96px;font-variant-numeric:tabular-nums}
.p2pk b{color:var(--c4);font-weight:700;font-variant-numeric:tabular-nums}
.p2pk .p2m{margin-left:auto;text-align:right}
.p2note{margin:8px 0 0;padding:8px 10px;border:1px solid color-mix(in srgb,var(--c4) 45%,var(--ln));border-radius:10px;background:color-mix(in srgb,var(--c4) 8%,transparent);font-size:11.5px;line-height:1.45;color:var(--c4);overflow-wrap:anywhere}
/* P2.5: климат рядом с энергией — точки дней (SVG, без библиотек) и таблица */
.p25plots{display:flex;flex-wrap:wrap;gap:6px 24px;margin:8px 0 0}
.p25sc{flex:1 1 300px;max-width:360px;margin:2px 0 0;font-size:11px;color:var(--mut)}
.p25sc>span{display:block;margin-bottom:2px}
.p25plot{width:100%;max-width:340px;display:block;margin:2px 0}
.p25ax{font-size:9px;fill:var(--mut)}
#climTableWrap{margin-top:10px}
#climTbl td{font-variant-numeric:tabular-nums}
@media(max-width:600px){#climTbl th,#climTbl td{padding:5px 4px;font-size:11px}}
/* P2.3: полоса прогресса месяца (0 — лимит, 100 — факт) */
.p34bar{height:8px;border-radius:99px;background:var(--grid);overflow:hidden;margin:-2px 0 8px}
.p34bar i{display:block;height:100%;width:0;border-radius:99px;background:var(--c1)}
.p34bar.over i{background:var(--red)}
#budState,#expState{margin:6px 0 0;font-size:11.5px;color:var(--c4)}
#budState:empty,#expState:empty{display:none}
@media print{.p2pk{border-color:#888}}

)CSS";

static const char APP_JS[] PROGMEM = R"JS(
"use strict";
var BOOT_DATE=document.documentElement.dataset.bootDate||"",BOOT_HOUR=+document.documentElement.dataset.bootHour||0;
var S={date:"",days:[],hist:null,today:"",watt:false,nums:true,last:0,dayMax:0,sel:null};
function G(i){return document.getElementById(i)}
function txt(i,t){var e=G(i);if(e)e.textContent=t}
function num(v,n){return (v==null||isNaN(v))?"—":(+v).toFixed(n==null?0:n)}
function hhmm(ts){if(!ts)return "—";var d=new Date(ts*1000);return("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)}
function fmtDate(s){if(!s)return "—";var p=s.split("-");var m=["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"];return (+p[2])+" "+m[(+p[1])-1]+" "+p[0]}
function niceMax(v){var e=Math.pow(10,Math.floor(Math.log10(v||1))),f=(v||1)/e,c=[1,1.2,1.5,2,2.5,3,4,5,6,8,10];for(var i=0;i<c.length;i++)if(f<=c[i]+1e-9)return c[i]*e;return 10*e}

/* ---------- скины (те же шесть, что на плате) ---------- */
var SKIN_NAMES=["Светлая","Тёмная","ЧБ","Розовая","Красная","Радужная"];
var SKIN_ICON=["☀","🌙","◐","🌸","🔥","🌈"];
function setSkin(n,noBoard){
  n=((n%6)+6)%6;
  document.documentElement.setAttribute("data-skin",String(n));
  try{localStorage.setItem("dom.skin",String(n))}catch(e){}
  var mt=document.querySelector('meta[name="theme-color"]');
  if(mt){ var BG=["#f8faff","#0b111b","#000000","#ffe0ec","#2e0808","#0c0a18"]; mt.setAttribute("content",BG[n]); }
  var b=G("themeBtn");
  if(b)b.textContent=SKIN_ICON[n]+" "+SKIN_NAMES[n];
  // тема браузера и тема платы независимы: плату не трогаем
}
function skinNext(){ var c=+(document.documentElement.getAttribute("data-skin")||1); setSkin(c+1); }
// ?skin=3 — посмотреть тему без сохранения (удобно для скриншотов и ссылок)
var FIXED_SKIN=null;
try{
  var q=new URLSearchParams(location.search).get("skin");
  if(q!=null && q!==""){ FIXED_SKIN=+q; setSkin(FIXED_SKIN,true); }
  else{
    var sk=localStorage.getItem("dom.skin");
    if(sk==null) sk=document.documentElement.getAttribute("data-skin");
    setSkin(sk==null?1:+sk,true);
  }
}catch(e){ setSkin(1,true); }

/* ---------- тема (совместимость) ---------- */
function setTheme(t){
  document.documentElement.setAttribute("data-theme",t);
  G("themeBtn").textContent = (t==="dark" ? "☀ Светлая" : "🌙 Тёмная");
  try{localStorage.setItem("dom.theme",t)}catch(e){}
}
G("themeBtn").addEventListener("click",function(){ skinNext() });
if(G("retryBtn")) G("retryBtn").addEventListener("click",function(){ poll() });
try{var th=localStorage.getItem("dom.theme"); if(th) document.documentElement.setAttribute("data-theme",th);}catch(e){}

/* ---------- живое состояние ---------- */
function applyEnv(d){
  try{
    var et=document.getElementById('envT');
    if(!et) return;
    if(d.eok){
      et.textContent=num(d.et,1);
      document.getElementById('envTS').textContent='датчик подключён';
      document.getElementById('envH').textContent=num(d.eh,0);
      document.getElementById('envP').textContent=num((d.ep||0)*0.75006,0);
      document.getElementById('envPS').textContent=num(d.ep,0)+' гПа';
    } else {
      et.textContent='—';
      document.getElementById('envTS').textContent='датчик не найден';
      document.getElementById('envH').textContent='—';
      document.getElementById('envP').textContent='—';
      document.getElementById('envPS').textContent='проверь /i2c';
    }
  }catch(e){}
}
function apply(d){
  if(!d)return;
  S.data=d; S.last=Date.now();
  var phaseKnown=d.vSeen==null?+d.v>50:!!d.vSeen, powerKnown=d.pSeen==null?d.p!=null&&phaseKnown:!!d.pSeen, tempKnown=d.tSeen==null?d.temp!=null&&!!d.tAt:!!d.tSeen, phaseStale=!!d.stale;
  txt("mv",num(phaseKnown?d.v:null)); txt("mp",num(powerKnown?d.p:null)); txt("ma",num(phaseKnown?d.amp:null,2)); txt("mt",num(tempKnown?d.temp:null,1));
  txt("ptar",num(d.tariff,2)); txt("pe",num(d.e,2));
  txt("tkwh",num(d.tkwh,2)); txt("tcost",num(d.tcost,2));
  applyEnv(d);
  txt("ykwh",num(d.ykwh,2)); txt("ykost",num(d.ykost,2));
  txt("mkwh",num(d.mkwh,2)); txt("mcost",num(d.mcost,2));
  txt("cRssi",num(d.rssi)+" дБм");
  txt("dVbig",num(phaseKnown?d.v:null)); txt("dAbig",num(phaseKnown?d.amp:null,2)); txt("dTbig",num(tempKnown?d.temp:null,1));
  txt("dVnow",!phaseKnown?"ожидание данных":(phaseStale?"устарело · ":"получено ")+hhmm(d.vAt)); txt("dAnow",!powerKnown?"ожидание данных":(+d.pAge>180?"устарело · ":"получено ")+hhmm(d.pAt)); txt("dTnow",!tempKnown?"ожидание данных":(+d.tAge>180?"устарело · ":"получено ")+hhmm(d.tAt));
  txt("mvAge","получено "+hhmm(d.vAt)); txt("mtAge","получено "+hhmm(d.tAt));
  txt("peAge","получено "+hhmm(d.eAt)); txt("tkwhAge","получено "+hhmm(d.eAt));
  var rel=G("rel");
  if(rel){rel.innerHTML='<i class="dot '+(d.r?"ok":"")+'"></i>'+(d.r?"ВКЛ":"ВЫКЛ");rel.style.color=d.r?"var(--c2)":"var(--red)"}
  txt("pcos","cos φ "+num((phaseKnown&&powerKnown&&d.amp>0.001)?d.p/(d.v*d.amp):null,2));
  var on=(d.disp===1||d.disp==="1");
  txt("scrState",on?"включена":"выключена");
  var bp=d.bright!=null?Math.round(d.bright/2.55):0;
  txt("scrBright",bp+" %");
  var rg=G("scrBrightRange"); if(rg&&document.activeElement!==rg)rg.value=Math.max(5,bp);
  var id=G("scrIdle"); if(id&&document.activeElement!==id)id.value=(d.idle!=null?d.idle:0);
  txt("scrEco",!on?"экран погашен":(d.idle>0?"гаснет через "+Math.round((d.idleMs||0)/60000)+" мин":"автогашение выкл"));
  txt("wxNow",(d.wDesc||"—")+" · "+num(d.wT,1)+" °C · дождь "+num(d.wPop)+" %");
  txt("wxTom",+d.wCodeT>0?num(d.wTminT)+"…"+num(d.wTmaxT)+" °C · дождь "+num(d.wPopT)+" %":"ожидание прогноза");
  txt("fc",num(d.mkwh/new Date().getDate()*30,1)+" кВт·ч");
  ring(d);
  if(S.hist&&S.date===S.today)drawE(S.hist);
  if(typeof d.gaps!=="undefined") gapShow(d.gaps, d.gapMin);
  if(window.p2OnData)window.p2OnData();   /* P2.1/P2.2: /data уже получен */
}
function ring(d){
  var pw=+d.p||0;
  var mx=Math.max(S.dayMax*1.15, pw*1.3, 300);
  mx=niceMax(mx);
  var f=Math.max(0.01,Math.min(1,pw/mx));
  var el=G("pfill");
  if(el){
    el.style.width=(f*100).toFixed(1)+"%";
    el.style.background = pw>=mx*0.85 ? "var(--red)" : (pw>=mx*0.5 ? "var(--c4)" : "var(--c1)");
  }
  txt("gcap","шкала "+Math.round(mx)+" Вт");
}
function netbar(on){
  var b=G("netbar");
  if(b) b.className = on ? "netbar on" : "netbar";
}
var POLL_BUSY=false, POLL_FAILS=0;
function poll(){
  if(POLL_BUSY)return;
  POLL_BUSY=true;
  fetch("/data",{cache:"no-store",timeoutMs:8000}).then(function(r){
    if(!r.ok) throw 0;
    return r.json();
  }).then(function(d){
    POLL_FAILS=0;
    document.documentElement.setAttribute("data-loading","0");
    netbar(false);
    txt("cNet",d.net?"интернет доступен":"локальная сеть · нет интернета");
    // тема браузера своя: скин платы не подхватываем
    apply(d);
    // P1.2: источник/свежесть (/measure) и качество данных (/storage).
    // Работает в этом же цикле poll; своего таймера нет.
    if(window.domStatusTick) window.domStatusTick(d);
  }).catch(function(){
    // Один сбой — не повод писать «нет связи»: плата бывает занята чтением
    // истории 5-15 с и отвечает с задержкой. Плашку показываем после 2 сбоев
    // подряд и сразу пробуем снова, чтобы связь восстановилась без F5.
    POLL_FAILS++;
    txt("cNet","нет связи");
    txt("liveAge","нет связи");
    if(POLL_FAILS>=2) netbar(true);
    document.documentElement.setAttribute("data-loading","0");
    if(window.domStatusTick) window.domStatusTick(null);
    setTimeout(function(){ if(!POLL_BUSY) poll() },2500);
  }).then(function(){POLL_BUSY=false})
}
try{document.documentElement.setAttribute("data-loading","1")}catch(e){}
setInterval(function(){if(!S.last)return;var s=Math.round((Date.now()-S.last)/1000);
  // 1-20 с — нормальная задержка; >20 с — данные устарели, но это НЕ значит,
  // что связи нет: плата может читать историю. «Нет связи» пишет только poll
  // после двух сбоев подряд (POLL_FAILS).
  var line=(s>20?"данные устарели ":"обновлено ")+s+" с";
  txt("cUpd",line);txt("liveAge",s>20?"устарели "+s+" с":s+" с")},1000);

/* ---------- история ---------- */
function loadDays(cb){
  domJson("/days").then(function(j){
    S.days=j.days||[]; S.today=S.days[0]||""; if(!S.date) S.date=S.today;
    loadHist(cb);
  }).catch(function(){txt('historySummary','Не удалось загрузить список дней. Повторите обновление.');if(cb)cb()});
}

function loadHist(cb){
  if(!S.date){if(cb)cb();return}
  var requestedDate=S.date;
  domJson("/history?date="+requestedDate).then(function(j){
    if(S.date!==requestedDate)return;
    S.hist=j;
    var mx=0,k;for(k=0;k<24;k++){var a=(j.hours||[])[k]||{};if(+a.pmax>mx)mx=+a.pmax}
    S.dayMax=mx;
    drawNav(); drawE(j); drawVT(j); renderDetails(j); if(window.historyAccept)window.historyAccept(j); if(window.p2OnHist)window.p2OnHist(j); if(cb)cb();
  }).catch(function(){if(cb)cb()});
}

function renderDetails(j){
  var h=(j&&j.hours)||[],i;
  var V=[],A=[],T=[];
  for(i=0;i<24;i++){
    var a=h[i]||{};
    V.push(a.v>50?a.v:null);
    A.push((a.v>50&&a.p>0)?(a.p/a.v):null);
    T.push(a.t==null?null:a.t);
  }
  stat24("dV",V,"",function(x){return Math.round(x)});
  stat24("dA",A,"",function(x){return x.toFixed(2)});
  stat24("dT",T,"",function(x){return x.toFixed(1)});
  mini("dChartV",V);
  mini("dChartA",A);
  mini("dChartT",T);
}
function stat24(prefix,arr,u,fmt){
  var mn=null,mx=null,sum=0,n=0,mnI=-1,mxI=-1,i;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;
    if(mn===null||v<mn){mn=v;mnI=i}
    if(mx===null||v>mx){mx=v;mxI=i}
    sum+=v;n++}
  txt(prefix+"min",mn===null?"—":fmt(mn));
  txt(prefix+"avg",n?fmt(sum/n):"—");
  txt(prefix+"max",mx===null?"—":fmt(mx));
  txt(prefix+"minT",mnI<0?"—":("0"+mnI).slice(-2)+":00");
  txt(prefix+"maxT",mxI<0?"—":("0"+mxI).slice(-2)+":00");
}
function mini(id,arr){
  var el=G(id); if(!el)return;
  var W=300,H=64,pl=2,pr=2,pt=6,pb=6,slot=(W-pl-pr)/24,i;
  var mn=null,mx=null;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;if(mn===null||v<mn)mn=v;if(mx===null||v>mx)mx=v}
  if(mn===null){el.innerHTML="";return}
  if(mx-mn<0.5){mx=mn+0.5}
  function y(v){return pt+(1-(v-mn)/(mx-mn))*(H-pt-pb)}
  var d="",pen=false;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;
    d+=(pen?"L":"M")+(pl+slot*(i+0.5)).toFixed(1)+" "+y(v).toFixed(1)+" ";pen=true}
  var area=d;
  for(i=23;i>=0;i--){var v2=arr[i];if(v2==null)continue;area+="L"+(pl+slot*(i+0.5)).toFixed(1)+" "+(H-pb).toFixed(1)+" "}
  var s='<path d="'+area+'Z" fill="var(--c1)" opacity=".10"/>';
  s+='<path d="'+d+'" fill="none" stroke="var(--c1)" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"/>';
  s+='<text class="ax" x="'+(W-pr)+'" y="'+(pt+6)+'" text-anchor="end">'+num(mx,1)+'</text>';
  s+='<text class="ax" x="'+(W-pr)+'" y="'+(H-1)+'" text-anchor="end">'+num(mn,1)+'</text>';
  el.innerHTML=s;
}

function drawNav(){
  var i=S.days.indexOf(S.date);
  G("prev").disabled=!(i>=0&&i<S.days.length-1); G("next").disabled=!(i>0);
  txt("dateLabel",fmtDate(S.date)+(S.date===S.today?" · сегодня":""));
  var g=(S.hist&&S.hist.gaps)||null;
  var box=G("gapBox");
  if(box){box.style.display="none"}
}
function drawE(j){
  var h=j.hours||[],W=900,H=190,pl=12,pr=10,pt=14,pb=18,slot=(W-pl-pr)/24,base=H-pb,hh=base-pt,am=0,pm=0,i;
  for(i=0;i<24;i++){var a=h[i]||{};var vv=S.watt?+a.p||0:+a.kwh||0;if(vv>am)am=vv;if(+a.pmax>pm)pm=+a.pmax}
  var s='';
  s+='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="var(--grid)"/>';
  for(i=0;i<3;i++){var y=pt+(base-pt)*i/2.4;
    s+='<line x1="'+pl+'" y1="'+y.toFixed(1)+'" x2="'+(W-pr)+'" y2="'+y.toFixed(1)+'" stroke="var(--grid)" stroke-dasharray="2 4" opacity=".7"/>'}
  for(i=0;i<24;i++){
    var b=h[i]||{},v=b.n?(S.watt?+b.p||0:+b.kwh||0):0,x=pl+i*slot+slot*0.2,w=slot*0.6;
    if(v>0){var bh=Math.max(2,v/(am||1)*hh*0.66);
      s+='<rect class="bar" data-i="'+i+'" x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="var(--bar1)"/>';
      if(S.nums)s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(base-bh-3).toFixed(1)+'" text-anchor="middle">'+(S.watt?Math.round(v):num(v,2))+'</text>'}
    if(b.n&&+b.pmax>0){var ph=Math.max(2,(+b.pmax)/(pm||1)*hh*0.22);
      s+='<rect class="bar2" data-i="'+i+'" x="'+x.toFixed(1)+'" y="'+(base-ph).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+ph.toFixed(1)+'" rx="2" fill="var(--bar2)" opacity=".9"/>'}
  }
  for(i=0;i<24;i++){var hx=pl+i*slot;s+='<rect class="hit" data-i="'+i+'" x="'+hx.toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(base-pt)+'" fill="transparent"/>'}
  s+='<text class="ax" x="'+pl+'" y="'+(pt-4)+'">'+(S.watt?"средняя мощность, Вт":"энергия за час, кВт·ч")+'</text>';
  for(i=0;i<=3;i++){var xx=pl+(W-pl-pr)*i/3;s+='<text class="ax" x="'+xx.toFixed(1)+'" y="'+(H-3)+'" text-anchor="middle">'+(i*8)+'</text>'}
  G("chE").innerHTML=s;
  hit();
}
function hit(){
  var svg=G("chE");
  function select(i,ev){
    S.sel=+i;
    svg.querySelectorAll("rect.bar,rect.bar2").forEach(function(r){
      r.classList.toggle("sel", r.getAttribute("data-i")===i && r.classList.contains("bar"));
      r.classList.toggle("sel2", false);
    });
    tipShow(i,ev);
  }
  svg.querySelectorAll("rect.hit").forEach(function(r){
    r.addEventListener("pointerdown",function(ev){ev.preventDefault();select(this.getAttribute("data-i"),ev)});
  });
  if(S.sel!=null) select(S.sel);
}

function tipShow(i,ev){
  var h=(S.hist&&S.hist.hours)||[],x=h[+i]||{},t=G("tip");
  var from=("0"+(+i)).slice(-2)+":00",to=("0"+(+i+1)).slice(-2)+":00";
  var html="<b>"+from+"–"+to+"</b>";
  if(x.n){html+="<br>энергия <b>"+num(x.kwh,2)+" кВт·ч</b>";
    html+="<br>средняя <b>"+num(x.p,0)+" Вт</b>";
    if(x.pmax>0){html+="<br>пик <b>"+num(x.pmax,0)+" Вт</b>"+(x.pmaxT?" в "+x.pmaxT:"")+(x.pmaxDur>0?", "+Math.max(1,Math.round(x.pmaxDur/60))+" мин":"")}}
  else html+="<br>нет данных";
  t.innerHTML=html; t.style.display="block";
  var w=250,px=(ev.clientX||100),py=(ev.clientY||100);
  var left=Math.min(window.innerWidth-w-8,Math.max(8,px-w/2));
  var top=py-t.offsetHeight-16;                 // всегда ВЫШЕ курсора
  if(top<8) top=Math.min(window.innerHeight-t.offsetHeight-8,py+18);  // у верха — под курсором
  t.style.left=left+"px";
  t.style.top=top+"px";
}
function drawVT(j){
  var h=j.hours||[],W=900,H=140,pl=12,pr=10,pt=8,pb=14,slot=(W-pl-pr)/24,i;
  var vs=[],ts=[];
  for(i=0;i<24;i++){var a=h[i]||{};vs.push(+a.n>0?(+a.v||0):0);ts.push(+a.n>0&&a.t!=null?+a.t:null)}
  var vmin=999,vmax=0,tmin=99,tmax=-99;
  for(i=0;i<24;i++){if(vs[i]>50){vmin=Math.min(vmin,vs[i]);vmax=Math.max(vmax,vs[i])}if(ts[i]!=null){tmin=Math.min(tmin,ts[i]);tmax=Math.max(tmax,ts[i])}}
  if(vmax<=0)vmax=250; if(vmin>vmax)vmin=vmax-10; if(tmax<tmin){tmin=20;tmax=25}
  function vy(v){return pt+(1-(v-vmin)/((vmax-vmin)||1))*(H-pt-pb)}
  function ty(t){return pt+(1-(t-tmin)/((tmax-tmin)||1))*(H-pt-pb)}
  var sv='',st='',pv=false,ptt=false;
  for(i=0;i<24;i++){var x=pl+slot*(i+0.5);
    if(vs[i]>50){sv+=(pv?'L':'M')+x.toFixed(1)+' '+vy(vs[i]).toFixed(1)+' ';pv=true}
    if(ts[i]!=null){st+=(ptt?'L':'M')+x.toFixed(1)+' '+ty(ts[i]).toFixed(1)+' ';ptt=true}}
  var s='<line x1="'+pl+'" y1="'+(H-pb)+'" x2="'+(W-pr)+'" y2="'+(H-pb)+'" stroke="var(--grid)"/>';
  if(sv)s+='<path d="'+sv+'" fill="none" stroke="#60a5fa" stroke-width="2"/>';
  if(st)s+='<path d="'+st+'" fill="none" stroke="#f472b6" stroke-width="2"/>';
  var sel=S.selV;
  if(sel!=null&&sel<24){
    var sx=pl+slot*(sel+0.5);
    if(vs[sel]>50)s+='<circle cx="'+sx.toFixed(1)+'" cy="'+vy(vs[sel]).toFixed(1)+'" r="3.6" fill="#60a5fa" stroke="#fff" stroke-width="1.6"/>';
    if(ts[sel]!=null)s+='<circle cx="'+sx.toFixed(1)+'" cy="'+ty(ts[sel]).toFixed(1)+'" r="3.6" fill="#f472b6" stroke="#fff" stroke-width="1.6"/>';
  }
  for(i=0;i<24;i++){s+='<rect class="hit2" data-i="'+i+'" x="'+(pl+i*slot).toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(H-pt-pb)+'" fill="transparent"/>'}
  s+='<text class="ax" x="'+pl+'" y="'+(H-1)+'">'+vmin.toFixed(0)+'–'+vmax.toFixed(0)+' В · '+tmin.toFixed(1)+'–'+tmax.toFixed(1)+' °C</text>';
  G("chVT").innerHTML=s;
  bindVT();
}
function bindVT(){
  var svg=G("chVT");
  svg.querySelectorAll("rect.hit2").forEach(function(r){
    r.addEventListener("pointerdown",function(ev){
      ev.preventDefault();
      S.selV=+this.getAttribute("data-i");
      drawVT(S.hist||{hours:[]});
      tipShowVT(S.selV,ev);
    });
  });
}
function tipShowVT(i,ev){
  var h=(S.hist&&S.hist.hours)||[],x=h[+i]||{},t=G("tip");
  var from=("0"+(+i)).slice(-2)+":00",to=("0"+(+i+1)).slice(-2)+":00";
  var html="<b>"+from+"–"+to+"</b>";
  if(x.v>50)html+="<br>напряжение <b>"+num(x.v,0)+" В</b>";
  if(x.t!=null)html+="<br>температура <b>"+num(x.t,1)+" °C</b>";
  if(!(x.v>50)&&x.t==null)html+="<br>нет данных";
  t.innerHTML=html; t.style.display="block";
  var w=250,px=(ev.clientX||100),py=(ev.clientY||100);
  var left=Math.min(window.innerWidth-w-8,Math.max(8,px-w/2));
  var top=py-t.offsetHeight-16;
  if(top<8)top=Math.min(window.innerHeight-t.offsetHeight-8,py+18);
  t.style.left=left+"px"; t.style.top=top+"px";
}

/* ---------- кнопки ---------- */
G("unitBtn").addEventListener("click",function(){
  S.watt=!S.watt; this.textContent=S.watt?"Вт":"кВт·ч"; this.classList.toggle("on",S.watt);
  if(S.hist)drawE(S.hist);
});
G("numBtn").addEventListener("click",function(){
  S.nums=!S.nums; this.classList.toggle("on",S.nums);
  if(S.hist)drawE(S.hist);
});
G("refBtn").addEventListener("click",function(){
  var b=this; b.textContent="…"; b.disabled=true;
  loadDays(function(){b.textContent="обновить";b.disabled=false;poll()});
});
G("prev").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>=0&&i<S.days.length-1){S.date=S.days[i+1];loadHist()}});
G("next").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>0){S.date=S.days[i-1];loadHist()}});
G("scrBtn").addEventListener("click",function(){
  fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
    var on=(d.disp===1||d.disp==="1");
    return fetch("/disp?"+(on?"on=0":"on=1"),{cache:"no-store"});
  }).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("scrBrightRange").addEventListener("input",function(){
  var v=+this.value; txt("scrBright",v+" %");
  fetch("/disp?bright="+v,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("scrIdle").addEventListener("change",function(){
  var n=+this.value; if(isNaN(n)||n<0||n>60)return;
  fetch("/disp?idle="+n,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("gapDayBtn").addEventListener("click",function(){
  var b=this; b.disabled=true; b.textContent="тяну…";
  fetch("/gapfill?run=1&from="+S.date+"&to="+S.date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var n0=(j&&j.gaps)||0,t0=Date.now(),last=-1,same=0;
    var iv=setInterval(function(){
      fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
        if(d.gaps!==last){last=d.gaps;same=0}else same++;
        b.textContent="осталось "+d.gaps+"…";
        if((!d.gapAct||same>=2)&&Date.now()-t0>5000){
          clearInterval(iv); b.textContent=(d.gaps<n0?"готово":"в облаке нет данных"); b.disabled=false; loadHist();
        }
      }).catch(function(){});
    },2500);
  }).catch(function(){b.disabled=false;b.textContent="ошибка"});
});

document.addEventListener("pointerdown",function(ev){
  var el=ev.target;
  if(el&&el.closest&&(el.closest("#chE")||el.closest("#chVT")))return;
  G("tip").style.display="none";
},true);
window.addEventListener("scroll",function(){G("tip").style.display="none"},{passive:true});
window.addEventListener("resize",function(){G("tip").style.display="none"});


var Q = {on:false, cut:false, week:false};

function gostUpdate(){
  fetch("/gost", {cache:"no-store"}).then(function(r){return r.json()}).then(function(g){
    if(!g || !g.n) return;
    txt("gostAvg", num(g.vavg,1));
    txt("gostMin", num(g.vmin,0)+" В");
    txt("gostMax", num(g.vmax,0)+" В");
    txt("gostBad", num(g.badPct,2)+" %");
    txt("gostN", g.n);
    txt("gostRange", num(g.lo,0)+"–"+num(g.hi,0)+" В");
    var bar=G("gostBar");
    if(bar){ bar.style.width = Math.min(100, Math.max(0, 100 - Math.min(100, g.badPct*10)))+"%";
             bar.style.background = g.badPct>5 ? "var(--c4)" : "var(--c2)"; }
  }).catch(function(){});
}

function vlogUpdate(){
  fetch("/vlog", {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var items=(j&&j.items)||[];
    txt("evCnt", items.length);
    var box=G("evList"); if(!box) return;
    if(!items.length){ box.innerHTML='<div class="ev"><span>пока событий нет</span></div>'; return; }
    var html="";
    for(var i=0;i<items.length && i<40;i++){
      var e=items[i], d=new Date((e.t||0)*1000);
      var hh=("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
      html += '<div class="ev"><span class="t">'+hh+'</span><span class="'+(e.k?"swl":"sag")+'">'+
              (e.k?"всплеск":"провал")+' '+e.v+' В</span><span>'+e.s+' с</span></div>';
    }
    box.innerHTML=html;
  }).catch(function(){});
}

function protRender(p){
  if(!p) return;
  Q.on = !!p.on; Q.cut = !!p.cut;
  txt("pvmax", p.vmax); txt("pvmin", p.vmin); txt("ptmax", p.tmax); txt("ppmax", p.pmax);
  var b=G("protOnBtn"); if(b){ b.textContent = Q.on ? "вкл" : "выкл"; b.className = "qbtn"+(Q.on?" on":""); }
  var c=G("protCut"); if(c){ c.textContent = "отключение реле: "+(Q.cut?"вкл":"выкл"); c.className="qbtn danger"+(Q.cut?" on":""); }
  txt("protTrig", p.trig||0);
  if(p.vmax) G("pvmax").value=p.vmax;
  if(p.vmin) G("pvmin").value=p.vmin;
  if(p.tmax) G("ptmax").value=p.tmax;
  if(p.pmax) G("pimax").value=Math.round(p.pmax/230);
}

function protUpdate(){
  fetch("/protect", {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
}

function protSet(qs){
  fetch("/protect?"+qs, {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
}

function initQuality(){
  var b=G("protOnBtn");
  if(b) b.addEventListener("click", function(){ protSet("on="+(Q.on?0:1)); });
  var c=G("protCut");
  if(c) c.addEventListener("click", function(){
    if(!Q.cut && !confirm("Разрешить плате физически отключать реле при аварии?")) return;
    protSet("cut="+(Q.cut?0:1));
  });
  function vals(){ return "vmax="+G("pvmax").value+"&vmin="+G("pvmin").value+
                          "&tmax="+G("ptmax").value+"&imax="+G("pimax").value; }
  var ap=G("protApply");
  if(ap) ap.addEventListener("click", function(){
    if(!confirm("Записать эти пороги в автомат Tuya? Они будут работать даже при выключенной плате.")) return;
    var b=this; b.disabled=true; b.textContent="пишу…";
    fetch("/protect?apply=1&"+vals(), {cache:"no-store"}).then(function(r){return r.json()}).then(function(p){
      protRender(p);
      b.textContent = (p.apply && (p.apply.a1||p.apply.a2)) ? "записано" : "ошибка";
      setTimeout(function(){ b.textContent="записать в автомат"; b.disabled=false; }, 2500);
    }).catch(function(){ b.textContent="ошибка"; b.disabled=false; });
  });
  var lo=G("protLocal");
  if(lo) lo.addEventListener("click", function(){
    fetch("/protect?"+vals(), {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
  });
  var tt=G("protTest");
  if(tt) tt.addEventListener("click", function(){
    fetch("/protect?test="+G("ptestv").value, {cache:"no-store"}).then(function(r){return r.json()}).then(function(p){
      txt("ptestres", (p.test && p.test.action) ? p.test.action : "—");
    }).catch(function(){});
  });
  var wb=G("weekBtn");
  if(wb) wb.addEventListener("click", function(){
    Q.week=!Q.week; this.classList.toggle("on", Q.week);
    G("chW").style.display = Q.week ? "block" : "none";
    if(Q.week) drawWeek();
  });
  gostUpdate(); vlogUpdate(); protUpdate();
  setInterval(gostUpdate, 15000);
  setInterval(vlogUpdate, 20000);
  setInterval(protUpdate, 20000);
}

function drawWeek(){
  var days=S.days.slice(0,7);
  var sums=new Array(days.length), loaded=0;
  days.forEach(function(d,idx){
    fetch("/history?date="+d, {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
      /* Сумма за сутки только по известным kwh; null — пробел, а не 0.
         known < total → сумма неполная и помечается «≥». */
      var h=(j&&j.hours)||[], s=null, known=0, total=0;
      for(var i=0;i<h.length;i++){
        var r=h[i];
        if(!r||!(+r.n>0))continue;
        total++;
        if(r.kwh!=null&&isFinite(+r.kwh)){s=(s==null?0:s)+(+r.kwh);known++}
      }
      sums[idx]={kwh:s,known:known,total:total};
    }).catch(function(){ sums[idx]={kwh:null,known:0,total:0} }).then(function(){
      loaded++;
      if(loaded===days.length) renderWeek(days.slice().reverse(), sums.slice().reverse());
    });
  });
}

function renderWeek(days, sums){
  var W=900,H=150,pl=14,pr=10,pt=16,pb=22,i;
  var slot=(W-pl-pr)/Math.max(1,days.length), base=H-pb, mx=0;
  for(i=0;i<sums.length;i++){var v=sums[i]&&sums[i].kwh;if(v!=null&&v>mx)mx=v}
  var s='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="var(--grid)"/>';
  for(i=0;i<days.length;i++){
    var e=sums[i]||{kwh:null,known:0,total:0}, val=e.kwh, x=pl+i*slot+slot*0.18, w=slot*0.64;
    var bh = val==null ? 0 : (val===0 ? 2 : (mx>0 ? Math.max(2,val/mx*(base-pt)*0.8) : 2));
    var tail = e.known<e.total ? '≥ ' : '';
    var tip = days[i]+': '+(val==null?'нет измерений':tail+val.toFixed(2)+' кВт·ч')+
              (e.total?' · часов с данными '+e.known+' из '+e.total:'');
    if(bh>0) s+='<rect x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="var(--bar1)"><title>'+tip+'</title></rect>';
    else s+='<rect x="'+x.toFixed(1)+'" y="'+(base-2).toFixed(1)+'" width="'+w.toFixed(1)+'" height="2" rx="1" fill="var(--grid)"><title>'+tip+'</title></rect>';
    s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(base-bh-4).toFixed(1)+'" text-anchor="middle">'+(val==null?'—':tail+val.toFixed(2))+'</text>';
    s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(H-6)+'" text-anchor="middle">'+days[i].slice(8)+'.'+days[i].slice(5,7)+'</text>';
  }
  G("chW").innerHTML=s;
}


var SCH = {cnt:0, rules:[], allowOff:0};

function schRender(j){
  if(!j) return;
  SCH = j;
  var box=G("schList"); if(!box) return;
  var b=G("allowBtn");
  if(b){ b.textContent = "выключение: " + (j.allowOff?"разрешено":"запрещено");
         b.className = "qbtn danger" + (j.allowOff?" on":""); }
  if(!j.rules || !j.rules.length){ box.innerHTML='<div class="r"><span>правил пока нет</span></div>'; return; }
  var html="";
  for(var i=0;i<j.rules.length;i++){
    var r=j.rules[i];
    var hh=("0"+r.h).slice(-2)+":"+("0"+r.m).slice(-2);
    html += '<div class="r"><span class="t">'+hh+'</span><span>'+((r.on)?"включить":"выключить")+'</span>'+
            '<span class="days">дни '+r.days+'</span><span class="days">'+(r.en?"вкл":"выкл")+'</span>'+
            '<span style="margin-left:auto"><button class="qbtn" data-del="'+i+'">удалить</button></span></div>';
  }
  box.innerHTML=html;
  box.querySelectorAll("button[data-del]").forEach(function(btn){
    btn.addEventListener("click", function(){
      var rule=SCH.rules[+this.getAttribute("data-del")];
      SCH.rules.splice(+this.getAttribute("data-del"),1);
      schSave();
    });
  });
}

function schSave(){
  var parts=[];
  for(var i=0;i<SCH.rules.length;i++){
    var r=SCH.rules[i];
    parts.push(("0"+r.h).slice(-2)+":"+("0"+r.m).slice(-2)+","+(r.on?"on":"off")+","+r.days+","+(r.en?1:0));
  }
  fetch("/sched?set="+encodeURIComponent(parts.join(";")), {cache:"no-store"})
    .then(function(r){return r.json()}).then(schRender).catch(function(){});
}

function schLoad(){
  fetch("/sched", {cache:"no-store"}).then(function(r){return r.json()}).then(schRender).catch(function(){});
}

function initSched(){
  var b=G("allowBtn");
  if(b) b.addEventListener("click", function(){
    if(!SCH.allowOff && !confirm("Разрешить плате выключать реле по расписанию и по команде из Home Assistant?")) return;
    fetch("/sched?allow="+(SCH.allowOff?0:1), {cache:"no-store"}).then(function(r){return r.json()}).then(schRender).catch(function(){});
  });
  var a=G("schAdd");
  if(a) a.addEventListener("click", function(){
    var tv=(G("schTime").value||"").trim();
    var m=/^(\d{1,2}):(\d{2})$/.exec(tv);
    if(!m){ alert("время в формате ЧЧ:ММ"); return; }
    var h=+m[1], mi=+m[2];
    if(h>23||mi>59){ alert("неверное время"); return; }
    var act = G("schAct").value === "on";
    if(!act && !SCH.allowOff){ if(!confirm("Выключение запрещено. Разрешить?")) return;
      fetch("/sched?allow=1",{cache:"no-store"}).then(function(){ SCH.allowOff=1; }); }
    SCH.rules.push({h:h,m:mi,on:act?1:0,days:(+G("schDays").value||127),en:1});
    schSave();
  });
  schLoad();
}


function tuyaUpdate(){
  fetch("/tuya", {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    function dec(b64){
      try{
        var s = atob(b64), out=[];
        for(var i=0;i+3<s.length;i+=4){
          out.push({c:s.charCodeAt(i), e:s.charCodeAt(i+1),
                    v:(s.charCodeAt(i+2)<<8)|s.charCodeAt(i+3)});
        }
        return out;
      }catch(e){ return []; }
    }
    var a1=dec(j.a1||""), a2=dec(j.a2||"");
    // набор 2 — электрика, набор 1 — температура
    var n2={1:"ток",3:"U макс",4:"U мин",8:"утечка"};
    var u2={1:" А",3:" В",4:" В",8:" мА"};
    var n1={3:"t мин",4:"t ср",5:"t макс"};
    var parts=[];
    a2.forEach(function(x){ if(x.e) parts.push((n2[x.c]||("код "+x.c))+" "+x.v+(u2[x.c]||"")); });
    a1.forEach(function(x){ if(x.e) parts.push((n1[x.c]||("код "+x.c))+" "+x.v+" °C"); });
    txt("tuyaS", parts.length ? parts.join(" · ") : "нет данных");
  }).catch(function(){});
}


// ---------------------------------------------------------------- статистика за период
var RNG={from:"",to:"",rows:[],on:""};
function pad2(n){return (n<10?"0":"")+n}
function iso(d){return d.getFullYear()+"-"+pad2(d.getMonth()+1)+"-"+pad2(d.getDate())}
function addDays(d,n){var x=new Date(d.getTime());x.setDate(x.getDate()+n);return x}
function rngPeriod(p){
  var now=new Date(), a, b=new Date(now.getTime());
  if(p=="today"){a=new Date(now.getTime());}
  else if(p=="yest"){a=addDays(now,-1);b=addDays(now,-1);}
  else if(p=="d2"){a=addDays(now,-2);b=addDays(now,-2);}
  else if(p=="d7"){a=addDays(now,-6);}
  else if(p=="d30"){a=addDays(now,-29);}
  else if(p=="week"){var w=(now.getDay()+6)%7;a=addDays(now,-w);}
  else if(p=="month"){a=new Date(now.getFullYear(),now.getMonth(),1);}
  else if(p=="quarter"){a=new Date(now.getFullYear(),Math.floor(now.getMonth()/3)*3,1);}
  else if(p=="half"){a=new Date(now.getFullYear(),now.getMonth()<6?0:6,1);}
  else if(p=="prev"){a=new Date(now.getFullYear(),now.getMonth()-1,1);b=new Date(now.getFullYear(),now.getMonth(),0);}
  else if(p=="year"){a=new Date(now.getFullYear(),0,1);}
  else if(p=="ytd"){a=new Date(now.getFullYear(),0,1);}
  else {a=new Date(now.getTime());}
  return {from:iso(a),to:iso(b)};
}
function rngMark(p){
  var bs=G("rngBtns").getElementsByTagName("button");
  for(var i=0;i<bs.length;i++){
    var on=bs[i].getAttribute("data-p")===p;
    bs[i].className=on?"on":"";
    bs[i].setAttribute("aria-pressed",on?"true":"false");
  }
}

/* ---------- графики периода: расход и температура по дням ---------- */

function drawBars(el, seq, hourly){
  var W=900,H=170,pl=34,pr=10,pt=18,pb=26;
  var mx=0; for(var i=0;i<seq.length;i++){ if(+seq[i].kwh>mx) mx=+seq[i].kwh }
  if(!(mx>0)) mx=1;
  var g2=niceMax(mx);
  var s='';
  for(var g=0;g<=2;g++){ var v=g2*g/2, yy=pt+(1-v/g2)*(H-pt-pb);
    s+='<line x1="'+pl+'" y1="'+yy.toFixed(1)+'" x2="'+(W-pr)+'" y2="'+yy.toFixed(1)+'" stroke="var(--grid)"/>';
    s+='<text class="bl" x="'+(pl-6)+'" y="'+(yy+3).toFixed(1)+'" text-anchor="end">'+num(v,g2<0.1?3:g2<1?2:g2<10?1:0)+'</text>';
  }
  var slot=(W-pl-pr)/seq.length, bw=Math.max(3,Math.min(38,slot*0.62));
  for(var i=0;i<seq.length;i++){
    var r=seq[i], x=pl+slot*i+(slot-bw)/2, v=+r.kwh||0;
    var yy=pt+(1-v/g2)*(H-pt-pb), hgt=H-pb-yy;
    var hot = g2>0 && v>=g2*0.85;
    var label = hourly ? '' : r.d;
    s+='<g class="bclick" data-day="'+(label||'')+'" data-index="'+i+'" style="cursor:pointer">';
    s+='<rect class="bar-hit" x="'+(pl+slot*i).toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(H-pt-pb)+'" fill="transparent"/>';
    s+='<rect class="bar-value" x="'+x.toFixed(1)+'" y="'+yy.toFixed(1)+'" width="'+bw.toFixed(1)+'" height="'+Math.max(1,hgt).toFixed(1)+
       '" rx="2" fill="'+(hot?'var(--c4)':'var(--c1)')+'" opacity="'+(hourly&&!r.ok?0.25:0.92)+'"/>';
    if(seq.length<=24 && v>0)
      s+='<text class="bv" x="'+(x+bw/2).toFixed(1)+'" y="'+(yy-4).toFixed(1)+'" text-anchor="middle">'+
         (hourly ? num(v,v<10?2:1) : (v<10?num(v,2):num(v,1)))+'</text>';
    var lab = hourly ? (r.hour+'') : (r.d.slice(8)+'.'+r.d.slice(5,7));
    if(!hourly && seq.length>16){
      var step = seq.length>25 ? 5 : Math.ceil(seq.length/12);
      if(i%step!==0) lab='';
    }
    if(hourly && i%3!==0) lab='';
    if(lab) s+='<text class="bl" x="'+(x+bw/2).toFixed(1)+'" y="'+(H-8)+'" text-anchor="middle">'+lab+'</text>';
    if(label) s+='<title>'+r.d+' · '+num(v,2)+' кВт·ч — нажми, чтобы открыть этот день</title>';
    s+='</g>';
  }
  el.innerHTML='<svg viewBox="0 0 '+W+' '+H+'" style="pointer-events:auto">'+s+'</svg>';
  el.__barRows=seq; el.__barHourly=hourly;
  // делегирование на контейнер: обработчик живёт дольше, чем SVG (его перерисовывают)
  if(!el.__barClick){
    el.__barClick = function(ev){
      var tgt = ev.target, g = null;
      while (tgt && tgt !== el) {
        if (tgt.getAttribute && tgt.getAttribute('class') === 'bclick') { g = tgt; break }
        tgt = tgt.parentNode;
      }
      if(!g) return;
      var idx=+g.getAttribute('data-index'), row=el.__barRows[idx];
      if(!row) return;
      if(el.__barHourly){
        if(ev.type==='click' && Date.now()-(el.__hourDownAt||0)<700) return;
        if(ev.type==='pointerdown') el.__hourDownAt=Date.now();
        ev.preventDefault();
        el.querySelectorAll('.bclick.selected').forEach(function(x){x.classList.remove('selected')});
        g.classList.add('selected');
        var detail=el.nextElementSibling;
        if(!detail || !detail.classList.contains('bar-detail')){detail=document.createElement('div');detail.className='bar-detail';el.parentNode.insertBefore(detail,el.nextSibling)}
        var h=+row.hour, data=row.data;
        detail.textContent=('0'+h).slice(-2)+':00–'+('0'+(h+1)).slice(-2)+':00 · '+(data && +data.n>0 ? 'энергия '+num(data.kwh,3)+' кВт·ч · средняя мощность '+num(data.p,0)+' Вт · пик '+num(data.pmax,0)+' Вт · напряжение '+num(data.v,1)+' В · температура '+num(data.t,1)+' °C' : 'нет данных');
        return;
      }
      var d = g.getAttribute('data-day'); if(!d) return;
      // pointerdown и click приходят парой: один выбор дня = один запрос.
      var now=Date.now();
      if(ev.type==='click' && el.__barDownDay===d && now-el.__barDownAt<700) return;
      if(ev.type==='pointerdown'){ el.__barDownDay=d; el.__barDownAt=now; }
      ev.preventDefault();
      openRangeDay(d);
    };
    el.addEventListener('pointerdown', el.__barClick, false);
    el.addEventListener('click', el.__barClick, false);
  }
}

/* ---------- раскрытие суток: профиль по часам прямо в карточке периода ---------- */
function openRangeDay(day){
  if(!/^\d{4}-\d{2}-\d{2}$/.test(day)) return;
  if(window.openMetricHistory){window.openMetricHistory('kwh',day);return}
  var from=G('rngFrom'), to=G('rngTo');
  if(from) from.value=day;
  if(to) to.value=day;
  rngMark('');
  rngLoad(day,day);
}

function rangeTempValid(r){return r.tvalid!==false && +r.rows>0 && r.tmin!=null && r.tmax!=null}
function escapeText(value){return String(value).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
/* Сутки «измерены», если плата отдала хотя бы одну строку. Пустые сутки /range
   присылает с rows:0 и kwh:0 — это не ноль расхода, а отсутствие измерений. */
function rngDayMeasured(r){return !!r&&(r.rows!=null?+r.rows>0:(r.kwh!=null||r.vvalid===true||+r.pmax>0))}
function rngRender(j){
  RNG.rows=j.rows||[];
  var t=j.totals||{};
  var days=+t.days||0,total=+t.daysTotal>0?+t.daysTotal:RNG.rows.length,missing=+t.missing||0,unknown=+t.unknownKwh||0;
  var kwhDays=+t.kwhDays||0;
  var live=!!(j.to&&S&&S.today&&j.to>=S.today);
  var partial=days>0&&(missing>0||unknown>0||live);
  var pre=partial?"≥ ":"";
  txt("rKwh",t.kwh==null?"—":pre+num(t.kwh,2));
  var kwhS=days?(days+(days==1?" день":" дней")+" с данными"+(missing?" · без измерений "+missing+(total?" из "+total:""):"")+(unknown?" · у "+unknown+" сут нет суммы кВт·ч":"")+(live?" · сегодняшние сутки ещё не закончились":"")):"нет измерений за период";
  txt("rKwhS",kwhS);
  txt("rCost",t.cost==null?"—":pre+num(t.cost,2)); txt("rCostS","средний тариф "+(t.kwh>0?(t.cost/t.kwh).toFixed(2):"—")+" ₽");
  txt("rDN",(t.dayKwh==null?"—":num(t.dayKwh,2))+" / "+(t.nightKwh==null?"—":num(t.nightKwh,2)));
  txt("rDNS",(t.dayCost==null?"—":num(t.dayCost,2))+" ₽ / "+(t.nightCost==null?"—":num(t.nightCost,2))+" ₽");
  txt("rAvg",t.avgDayKwh==null?"—":num(t.avgDayKwh,2));
  var avgS=[];
  if(t.vmin!=null&&t.vmax!=null)avgS.push("U "+t.vmin+"–"+t.vmax+" В");
  if(t.pmax!=null)avgS.push("пик "+t.pmax+" Вт");
  avgS.push(days?(missing||unknown?"среднее по доступным суткам ("+kwhDays+" из "+total+")":"среднее по "+days+" суткам"):"—");
  txt("rAvgS",avgS.join(" · "));
  txt("rngNote",(j.from||"")+" — "+(j.to||""));
  var tb=G("rngTbl").getElementsByTagName("tbody")[0], h="";
  for(var i=0;i<RNG.rows.length;i++){
    var r=RNG.rows[i],ok=rngDayMeasured(r);
    var cell=function(v,fmt){return ok&&v!=null&&isFinite(+v)?fmt(v):"—"};
    h+="<tr"+(ok?"":" class='mut'")+"><td>"+fmtDate(r.d)+(ok?"":" · нет измерений")+"</td><td>"+cell(r.kwh,function(v){return num(v,2)})+"</td><td>"+cell(r.cost,function(v){return num(v,2)})+"</td>"
      +"<td class='mut'>"+cell(r.dayKwh,function(v){return num(v,2)})+"</td><td class='mut'>"+cell(r.nightKwh,function(v){return num(v,2)})+"</td>"
      +"<td>"+(ok&&!(r.vvalid===false)&&+(r.vmax)>0?num(r.vmin,0)+"–"+num(r.vmax,0):"—")+"</td>"
      +"<td>"+(ok&&rangeTempValid(r)?num(r.tmin,1)+"–"+num(r.tmax,1):"—")+"</td>"
      +"<td class='mut'>"+cell(r.pmax,function(v){return num(v,0)})+"</td>"
      +"<td class='mut'>"+escapeText(r.w||"—")+"</td>"
      +"<td class='mut'>"+(ok&&r.wvalid!==false && r.wtmin!=null && r.wtmax!=null && (r.wvalid || r.w)? num(r.wtmin,1)+"–"+num(r.wtmax,1)+"°" : "—")+"</td></tr>";
  }
  if(!h) h="<tr><td colspan='10' class='mut'>нет данных за период</td></tr>";
  tb.innerHTML=h;
}
/* «Заполнить пропуски»: запуск + опрос /gapfill/status каждые 2 с.
   Показываем «собрано X из Y окон · вставлено N строк», по завершении — итог
   (msg) или причину (sdErr/sdMsg) и обновляем таблицу периода и график. */
var GAP_BUSY=false, GAP_TIMER=null, GAP_POLLS=0, GAP_DONE=0;
var GAP_PHASE=['простой','поиск пропусков','запрос облака','запись на карту'];
function gapPhase(p){ return GAP_PHASE[+p]||'работа' }
function gapShow(g,m){
  // Итог заполнения держим на экране минуту, фоновый /data его не затирает.
  if(GAP_BUSY||Date.now()-GAP_DONE<60000) return;
  txt("gapInfo",(g>0)?("пропусков "+g+", "+(m||0)+" мин"):"пропусков нет");
}
function gapFinish(text, ok){
  if(GAP_TIMER){ clearInterval(GAP_TIMER); GAP_TIMER=null }
  GAP_BUSY=false; GAP_DONE=Date.now();
  txt("gapInfo", text);
  var b=G("gapBtn"); if(b){ b.disabled=false; b.textContent="Заполнить пропуски" }
  // Плата заново пишет /energy.csv — просим новые данные, когда скан уже закончен.
  if(ok!==false) setTimeout(function(){
    try{ if(window.historyRefresh) window.historyRefresh(); }catch(e){}
    try{ loadDays(); }catch(e){}
  }, 900);
}
function gapProgress(s){
  var parts=[];
  var x=+s.gap||0, y=+s.gaps||0;
  parts.push('собрано '+Math.min(x,y)+' из '+y+' окон');
  parts.push('вставлено '+(s.ins!=null?s.ins:(s.insTotal||0))+' строк');
  if(s.phase!=null) parts.push(gapPhase(s.phase));
  return parts.join(' · ');
}
function gapFillRun(){
  if(GAP_BUSY) return;
  var f=RNG.from||(G("rngFrom")||{}).value||S.date||S.today;
  var t2=RNG.to||(G("rngTo")||{}).value||f;
  if(!f||!t2){ txt("gapInfo","сначала выбери период"); return }
  GAP_BUSY=true; GAP_POLLS=0;
  var b=G("gapBtn"); if(b){ b.disabled=true; b.textContent="заполняю…" }
  txt("gapInfo","запускаю…");
  fetch("/gapfill?run=1&from="+f+"&to="+t2,{cache:"no-store"}).then(function(r){ return r.json() }).then(function(j){
    if(!j||j.ok===false){ var er=j&&j.err?String(j.err):""; gapFinish("не запустилось"+(er?": "+(er==="sd"?"нет карты SD":er==="recovery busy"?"идёт восстановление карты":er):""),false); return }
    GAP_TIMER=setInterval(function(){
      if(++GAP_POLLS>450){ gapFinish("не дождался завершения за 15 минут",false); return }
      fetch("/gapfill/status",{cache:"no-store"}).then(function(r){ return r.json() }).then(function(s){
        if(s&&s.active){ txt("gapInfo",gapProgress(s)+' · '+(GAP_POLLS*2)+' с'); return }
        if(GAP_TIMER){ clearInterval(GAP_TIMER); GAP_TIMER=null }
        if(!s){ gapFinish("плата не ответила",false); return }
        var msg=(s.msg&&String(s.msg).trim)?String(s.msg).trim():"";
        if(+s.sdErr){ gapFinish("ошибка карты SD ("+s.sdErr+")"+(s.sdMsg?": "+s.sdMsg:msg?": "+msg:""),false); return }
        var ins=(s.insLast!=null?s.insLast:(s.ins!=null?s.ins:s.insTotal||0));
        var left=+s.gaps||0;
        gapFinish(msg?msg:("готово: вставлено "+ins+" строк"+(left?" · пропусков осталось "+left:"")),true);
      }).catch(function(){
        // одна неудача опроса — не повод бросать: плата занята записью на карту
        if(!GAP_TIMER) txt("gapInfo","нет ответа платы · продолжаю ждать…");
      });
    },2000);
  }).catch(function(){ gapFinish("ошибка запроса запуска",false) });
}
document.getElementById("gapBtn").addEventListener("click", gapFillRun);

function rngClear(){
  ["rKwh","rCost","rDN","rAvg","rKwhS","rCostS","rDNS","rAvgS"].forEach(function(i){ txt(i,"—"); });
  RNG.rows=[];
  G("rngTbl").getElementsByTagName("tbody")[0].innerHTML="<tr><td colspan='10' class='mut'>нет данных за период</td></tr>";
}
// период длиннее 31 дня плата считает порциями: 31 день за запрос
var RNG_PRINT_NOTE="";
function printStamp(){
  var el=G("rngNote"); if(!el) return;
  RNG_PRINT_NOTE=el.textContent;
  var n=new Date();
  var d=("0"+n.getDate()).slice(-2)+"."+("0"+(n.getMonth()+1)).slice(-2)+"."+n.getFullYear();
  var tm=("0"+n.getHours()).slice(-2)+":"+("0"+n.getMinutes()).slice(-2);
  el.textContent=el.textContent+" · сформировано "+d+" "+tm;
}
window.addEventListener("beforeprint", printStamp);
window.addEventListener("afterprint", function(){ if(RNG_PRINT_NOTE) G("rngNote").textContent=RNG_PRINT_NOTE; });

var RNG_SEQUENCE=0;
/* Единственный источник /range — history.js (там прогресс 202 и нарезка по 31 дню).
   Здесь только просьба загрузить диапазон, чтобы не было двух параллельных загрузок. */
function rngLoad(from,to){
  RNG_SEQUENCE++;
  if(window.p2Reset)window.p2Reset();   /* P2.1: новый период — прошлый итог не показываем */
  if(window.historyLoadRange){ window.historyLoadRange(from,to); return }
  txt("rngNote","история периода недоступна");
}
/* Итоги и таблица периода из уже полученных строк /range (без второго запроса).
   Суммируем только измеренные сутки и только известные значения: null — это
   «неизвестно», а не 0; пустые сутки (rows:0) в итог и среднее не входят. */
function rngFill(from,to,rows){
  rows=rows||[];
  RNG.from=from||""; RNG.to=to||""; RNG.rows=rows;
  var tot={kwh:null,cost:null,dayKwh:null,nightKwh:null,dayCost:null,nightCost:null,vmin:null,vmax:null,pmax:null};
  var measured=0,missing=0,kwhDays=0;
  for(var i=0;i<rows.length;i++){
    var r=rows[i];if(!r)continue;
    if(!rngDayMeasured(r)){missing++;continue}
    measured++;
    if(r.kwh!=null&&isFinite(+r.kwh)){tot.kwh=(tot.kwh==null?0:tot.kwh)+(+r.kwh);kwhDays++}
    if(r.cost!=null&&isFinite(+r.cost))tot.cost=(tot.cost==null?0:tot.cost)+(+r.cost);
    if(r.dayKwh!=null&&isFinite(+r.dayKwh))tot.dayKwh=(tot.dayKwh==null?0:tot.dayKwh)+(+r.dayKwh);
    if(r.nightKwh!=null&&isFinite(+r.nightKwh))tot.nightKwh=(tot.nightKwh==null?0:tot.nightKwh)+(+r.nightKwh);
    if(r.dayCost!=null&&isFinite(+r.dayCost))tot.dayCost=(tot.dayCost==null?0:tot.dayCost)+(+r.dayCost);
    if(r.nightCost!=null&&isFinite(+r.nightCost))tot.nightCost=(tot.nightCost==null?0:tot.nightCost)+(+r.nightCost);
    var vmn=+r.vmin,vmx=+r.vmax;
    if(r.vvalid!==false&&vmn>0&&(tot.vmin==null||vmn<tot.vmin))tot.vmin=vmn;
    if(r.vvalid!==false&&vmx>0&&(tot.vmax==null||vmx>tot.vmax))tot.vmax=vmx;
    if(r.pmax!=null&&isFinite(+r.pmax)&&(tot.pmax==null||+r.pmax>tot.pmax))tot.pmax=+r.pmax;
  }
  tot.days=measured; tot.daysTotal=rows.length; tot.missing=missing;
  tot.unknownKwh=measured-kwhDays; tot.kwhDays=kwhDays;
  tot.avgDayKwh=kwhDays?tot.kwh/kwhDays:null;
  rngRender({from:RNG.from,to:RNG.to,days:measured,daysTotal:rows.length,missing:missing,totals:tot,rows:rows});
  if(window.p2OnRange)window.p2OnRange(from,to,rows);   /* P2.1: сравнение с прошлым периодом */
}
window.rngFill=rngFill;
function rngApply(p){
  rngMark(p);
  var r=rngPeriod(p);
  G("rngFrom").value=r.from; G("rngTo").value=r.to;
  rngLoad(r.from,r.to);
}
function rngCsv(){
  // P2.4: экспорт с заголовком единиц, временной зоной и пустыми пропусками
  // живёт в page.html (там же блок «Экспорт и перенос»), чтобы не растить app.js.
  if(window.p34Csv){ var c=window.p34Csv(); if(c&&c.err){ alert(c.err); return } if(window.p34Download){ window.p34Download(); return } }
  if(!RNG.rows.length){ alert("нет данных за период"); return; }
  var s="\ufeffdate;kwh;cost;day_kwh;night_kwh;day_cost;night_cost;vmin;vmax;tmin;tmax;pmax\r\n";
  RNG.rows.forEach(function(r){
    s+=[r.d,r.kwh,r.cost,r.dayKwh,r.nightKwh,r.dayCost,r.nightCost,r.vmin,r.vmax,r.tmin,r.tmax,r.pmax].join(";")+"\r\n";
  });
  var a=document.createElement("a");
  a.href="data:text/csv;charset=utf-8,"+encodeURIComponent(s);
  a.download="energy-"+RNG.from+"_"+RNG.to+".csv"; a.click();
}
document.getElementById("rngBtns").addEventListener("click",function(e){
  var p=e.target.getAttribute&&e.target.getAttribute("data-p");
  if(p) rngApply(p);
});
rngMark("");   /* начальное состояние aria-pressed у пресетов периода */
G("rngGo").addEventListener("click",function(){
  var a=G("rngFrom").value,b=G("rngTo").value;
  if(!a||!b){ alert("укажи даты"); return; }
  if(a>b){ var x=a;a=b;b=x; }
  rngMark(""); rngLoad(a,b);
});
G("rngCsv").addEventListener("click",rngCsv);
G("rngPrint").addEventListener("click", function(){ window.print(); });


// ---------------------------------------------------------------- панель дня сверху
function daySync(){
  var dl=G("dateLabel"), l=G("dayLbl"), n=G("dayNote");
  var txt2 = dl ? dl.textContent : "—";
  if(l) l.textContent = txt2;
  if(n){
    var isToday = (S.date && S.date===S.today);
    n.textContent = isToday ? "текущие показания и история за сегодня"
                            : "текущие показания — сейчас; история — за " + txt2;
    n.style.color = isToday ? "" : "var(--c4)";
  }
}
function dayStep(dir){
  var p=G("prev"), nx=G("next");
  if(dir<0 && p) p.click(); else if(dir>0 && nx) nx.click();
  setTimeout(daySync, 350);
}
function dayTodayGo(){
  if(S.days && S.days.length){ S.date = S.days[0]; loadHist(); }
  setTimeout(daySync, 400);
}
(function(){
  var bp=G("dayPrev"), bn=G("dayNext"), bt=G("dayToday");
  /* role=button на <span> должен работать и с клавиатуры: Enter/пробел. */
  function dayKey(el,fn){
    if(!el) return;
    el.addEventListener("keydown",function(e){
      if(e.key==="Enter"||e.key===" "||e.key==="Spacebar"){e.preventDefault();fn()}
    });
  }
  if(bp){ bp.addEventListener("click", function(){ dayStep(-1); }); dayKey(bp,function(){ dayStep(-1) }); }
  if(bn){ bn.addEventListener("click", function(){ dayStep(1); }); dayKey(bn,function(){ dayStep(1) }); }
  if(bt){ bt.addEventListener("click", dayTodayGo); dayKey(bt,dayTodayGo); }
  setInterval(daySync, 1500);
  // старый переключатель дня внутри карточки «История» больше не нужен — он теперь сверху
  var pv=G("prev");
  if(pv && pv.parentNode) pv.parentNode.style.display="none";
})();
window.addEventListener("beforeprint", function(){
  // Вся статистика лежит в одном <details>: перед печатью раскрываем и, если
  // данные ещё не грузились, просим history.js загрузить их.
  var rep=G("historyWorkspace");
  if(rep){
    rep.open=true;
    if(window.historyEnsureLoaded){ try{ window.historyEnsureLoaded() }catch(e){} }
  }
  var m=G("printMeta"); if(!m) return;
  var fw=(document.querySelector(".ver")||{}).textContent||"";
  var td=document.getElementById("tarDay"), tn=document.getElementById("tarNight");
  var now=new Date();
  var d=("0"+now.getDate()).slice(-2)+"."+("0"+(now.getMonth()+1)).slice(-2)+"."+now.getFullYear();
  var tm=("0"+now.getHours()).slice(-2)+":"+("0"+now.getMinutes()).slice(-2);
  m.innerHTML = "Дом — энергия · отчёт за период · " + (RNG.from||"") + " — " + (RNG.to||"") +
    "<br>тариф: день " + (td?td.textContent:"—") + " · ночь " + (tn?tn.textContent:"—") +
    " · " + fw.trim() + " · сформировано " + d + " " + tm;
});

loadDays(); poll(); setInterval(function(){if(!document.hidden)poll()},3000); (function(){
  // Ленивая загрузка: при открытии страницы считаем только «Часы · сегодня».
  // /range (7/30 дней, месяц) запускается только по кнопке или по ссылке ?p=…
  var q=(location.search.match(/[?&]p=([a-z0-9]+)/)||[])[1];
  var ok=["today","yest","d2","d7","d30","week","month","quarter","half","prev","year","ytd"];
  if(q&&ok.indexOf(q)>=0&&q!=="today") setTimeout(function(){ rngApply(q) }, 0);
})(); initQuality(); initSched(); tuyaUpdate(); setInterval(tuyaUpdate, 60000);
/* ---------- звуки включения и перезагрузки ---------- */
var SND_BOOT=null;
function sndLoad(){
  fetch('/sound',{cache:'no-store'}).then(function(r){return r.json()}).then(function(s){
    SND_BOOT=s;
    ['boot','reboot'].forEach(function(key){
      var sel=G('snd'+(key==='boot'?'Boot':'Reboot'));
      if(!sel) return;
      var ev=(s.events||[]).filter(function(e){return e.key.indexOf(key)===0})[0];
      var cur=ev?+ev.preset:0;
      sel.innerHTML=(s.presets||[]).map(function(p){
        return '<option value="'+p.id+'"'+(p.id===cur?' selected':'')+'>'+p.name+'</option>';
      }).join('');
      sel.addEventListener('change',function(){
        fetch('/sound?'+key+'='+sel.value).then(function(){/* сохранено */});
      });
    });
  }).catch(function(){});
}
function sndPlay(key){
  fetch('/beep?ev='+key).then(function(){},function(){});
}
(function(){
  if(!G('sndBoot')) return;
  sndLoad();
  G('sndBootPlay').addEventListener('click',function(){ sndPlay('boot') });
  G('sndRebootPlay').addEventListener('click',function(){ sndPlay('reboot') });
})();

/* тариф и перезагрузка */
(function(){var b=G('tarSave');if(b)b.addEventListener('click',function(){var d=+G('tarDayIn').value,n=+G('tarNightIn').value,f=+G('tarFromIn').value,t=+G('tarToIn').value;if(!(d>0)||!(n>0)){txt('tarMsg','цена > 0');return}fetch('/tariff?set='+d+','+n+','+t+','+f).then(function(r){return r.text()}).then(function(){txt('tarMsg','сохранено');loadDays();poll()}).catch(function(){txt('tarMsg','ошибка')})});var rb=G('rebootBtn');if(rb)rb.addEventListener('click',function(){if(!confirm('Перезагрузить плату?'))return;txt('rebootMsg','…');fetch('/reboot').then(function(){txt('rebootMsg','отправлено')}).catch(function(){txt('rebootMsg','нет связи')})})})();

document.addEventListener('visibilitychange',function(){if(!document.hidden)poll()});

/* подпись «месяц»: с 1-го по сегодня */
(function(){
  var e=G('mkwhS'); if(!e) return;
  var d=new Date(), m=['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'];
  e.textContent='с 1 по '+d.getDate()+' '+m[d.getMonth()];
})();

)JS";

static const char HISTORY_JS[] PROGMEM = R"HJS(
/* История показателей — вариант 2: один блок, один ряд периодов.
   Правило простое: период задаёт пресет (Сегодня…С начала года) или свой
   диапазон, а вид графика подбирается сам:
     1 день  → столбики по часам (видно скачки за сутки);
     до 62 дней → столбики по дням;
     больше → столбики по неделям.
   Кнопками «Часы · Дни · Недели» вид можно переопределить вручную.
   Легенда (Расход, Пик мощности, …) отключает ряды в любом виде. */
(function(){
  var hourly=[['kwh','Энергия','кВт·ч',3],['p','Средняя мощность','Вт',0],['pmax','Пиковая мощность','Вт',0],['v','Напряжение','В',1],['t','Температура реле','°C',1]];
  var period=[['kwh','Расход','кВт·ч',3],['pmax','Пик мощности','Вт',0],['vmin','Напряжение мин','В',1],['vmax','Напряжение макс','В',1],['tmin','Температура мин','°C',1],['tmax','Температура макс','°C',1]];
  var colors={kwh:'#24c7e8',p:'#f3ad33',pmax:'#ed6b54',v:'#80aaff',vmin:'#6495ed',vmax:'#80c8ff',t:'#ef81bd',tmin:'#c873d9',tmax:'#ef81bd'}, hidden={};
  var MON=['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'];

  var mode='hours', date='', rangeFrom='', rangeMode=false;
  var selected=-1, points=[], request=0, loading=false, controller=null;
  var everLoaded=false, loadTries=0, loadedFrom='', loadedTo='';

  var host=G('historyWorkspace'), plot=host.querySelector('.history-combined svg');
  var root=G('historyMetrics'), legend=host.querySelector('.history-legend');

  function add(d,n){var x=new Date(d+'T12:00:00');x.setDate(x.getDate()+n);return iso(x)}
  function dayDiff(a,b){return Math.round((new Date(b+'T12:00:00')-new Date(a+'T12:00:00'))/86400000)}
  function isDate(s){return /^\d{4}-\d{2}-\d{2}$/.test(s)}
  function pair(a,b,unit){return a==null||b==null?'—':num(a,1)+'–'+num(b,1)+' '+unit}
  function delay(ms){return new Promise(function(r){setTimeout(r,ms)})}
  function status(t){G('historySummary').textContent=t}
  function hint(t){var el=G('historyHint');if(el)el.textContent=t}
  function dShort(s){var p=s.split('-');return (+p[2])+' '+MON[+p[1]-1]}
  function periodText(from,to){
    if(!isDate(from))return dShort(to);
    if(from===to)return dShort(to);
    var a=from.split('-'),b=to.split('-');
    if(a[0]===b[0]&&a[1]===b[1])return (+a[2])+'–'+(+b[2])+' '+MON[+b[1]-1];
    return dShort(from)+' – '+dShort(to);
  }
  function autoMode(from,to){var n=dayDiff(from,to);return n<=0?'hours':n<=62?'days':'weeks'}
  function modeTitle(){return mode==='hours'?'Показатели по часам':mode==='weeks'?'Показатели по неделям':'Показатели по дням'}
  function progressText(j){var kb=j&&+j.read>0?Math.round(+j.read/1024):0;return kb?'прочитано '+kb+' КБ · считаю …':'считаю …'}
  /* Честная математика: null/undefined — это «неизвестно», а не 0.
     Суммируем только известные значения, остальное помечаем «неполные данные». */
  function isNum(x){return x!=null&&x!==''&&isFinite(+x)}
  function addNum(a,x){return isNum(x)?(a==null?0:a)+(+x):a}
  function dm(s){return s.slice(8)+'.'+s.slice(5,7)}
  function todayIso(){var t=S&&S.today;if(isDate(t||''))return t;return iso(new Date())}
  function dayMeasured(x){return !!x&&(x.rows!=null?+x.rows>0:(isNum(x.kwh)||x.vvalid===true||+x.pmax>0))}
  function hourRange(item){return item.label+':00–'+('0'+(item.h+1)).slice(-2)+':00'}
  function escAttr(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
  function pointsForHours(d,j){
    var map={};(j&&j.hours||[]).forEach(function(r){if(r&&r.h!=null)map[+r.h]=r});
    var out=[];
    for(var i=0;i<24;i++){var r=map[i];out.push({label:('0'+i).slice(-2),date:d,h:i,valid:!!r&&(+r.n>0||(r.n==null&&r.kwh!=null)),values:r||{}})}
    return out;
  }
  /* Дни/недели: суммируем только известные kwh/cost, частичный бакет помечаем.
     Для недель конец бакета — min(start+6, to), будущие дни не считаются измеренными. */
  function pointsForPeriod(from,to,md,rows){
    var byDay={};
    (rows||[]).forEach(function(r){if(r&&isDate(r.d||''))byDay[r.d]=r});
    var step=(md==='weeks')?7:1,count=dayDiff(from,to)+1,today=todayIso(),out=[];
    for(var start=0;start<count;start+=step){
      var first=add(from,start);
      var last=step===1?first:(add(first,step-1)>to?to:add(first,step-1));
      var n=dayDiff(first,last)+1,list=[],k=0;
      for(k=0;k<n;k++){var day=byDay[add(first,k)];if(dayMeasured(day))list.push(day)}
      var val={kwh:null,cost:null,pmax:null,vmin:null,vmax:null,tmin:null,tmax:null},kwhKnown=0;
      list.forEach(function(x){
        if(isNum(x.kwh)){val.kwh=addNum(val.kwh,x.kwh);kwhKnown++}
        if(isNum(x.cost))val.cost=addNum(val.cost,x.cost);
        if(isNum(x.pmax))val.pmax=val.pmax==null?+x.pmax:Math.max(val.pmax,+x.pmax);
        var vv=x.vvalid==null?+x.vmax>0:!!x.vvalid,tv=x.tvalid==null?(+x.tmin!==0||+x.tmax!==0):!!x.tvalid;
        if(vv){
          if(isNum(x.vmin)&&+x.vmin>0)val.vmin=val.vmin==null?+x.vmin:Math.min(val.vmin,+x.vmin);
          if(isNum(x.vmax)&&+x.vmax>0)val.vmax=val.vmax==null?+x.vmax:Math.max(val.vmax,+x.vmax);
        }
        if(tv){
          if(isNum(x.tmin))val.tmin=val.tmin==null?+x.tmin:Math.min(val.tmin,+x.tmin);
          if(isNum(x.tmax))val.tmax=val.tmax==null?+x.tmax:Math.max(val.tmax,+x.tmax);
        }
      });
      var missing=n-list.length,unknownKwh=list.length-kwhKnown,live=first<=today&&last>=today;
      out.push({label:n===1?dm(first):dm(first)+'–'+dm(last),date:first,end:last,
                valid:list.length>0,measured:list.length,totalDays:n,missing:missing,kwhKnown:kwhKnown,unknownKwh:unknownKwh,
                partial:missing>0||unknownKwh>0||live,live:live,values:val});
    }
    return out;
  }
  function partialReason(p){
    var why=[];
    if(p.missing)why.push('нет измерений за '+p.missing+' из '+p.totalDays+' сут');
    if(p.unknownKwh)why.push('у '+p.unknownKwh+' сут нет суммы кВт·ч');
    if(p.live)why.push('текущие сутки ещё не закончились');
    return why.join('; ');
  }

  /* /range отвечает 202 {pending:true,retryMs,read} — ждём, показывая прогресс. */
  function rangeRequest(url,seq,onWait){
    var until=Date.now()+180000,fails=0;
    function attempt(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      return fetch(url,{cache:'no-store',signal:controller.signal,timeoutMs:15000}).then(function(r){
        return r.json().catch(function(){return {}}).then(function(j){return {status:r.status,j:j||{}}});
      }).then(function(x){
        if(seq!==request)throw Error('отменено');
        var j=x.j;
        if(x.status===202||j.pending){
          onWait(j);
          if(Date.now()>=until)throw Error('плата слишком долго считает период');
          return delay(Math.max(700,Math.min(3000,+j.retryMs||900))).then(attempt);
        }
        if(x.status>=500){var e=Error(j.err||('HTTP '+x.status));e.retryable=true;throw e}
        if(j.trunc)throw Error('плата вернула неполную историю — повторите загрузку');
        if(j.ok===false)throw Error(j.err||('HTTP '+x.status));
        return j;
      }).catch(function(error){
        if(seq!==request)throw error;
        var retry=error.retryable||error.name==='TypeError'||error.name==='AbortError'||error.name==='Read timeout';
        if(Date.now()<until&&retry&&fails<6){fails++;return delay(600*fails).then(attempt)}
        throw error;
      });
    }
    return attempt();
  }
  /* /history тоже асинхронный: отдаёт {pending:true,read,total}, пока читает CSV.
     Показываем прогресс и опрашиваем, пока день не будет готов. */
  function historyRequest(d,seq){
    var until=Date.now()+180000,fails=0;
    function attempt(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      return fetch('/history?date='+d,{cache:'no-store',signal:controller.signal,timeoutMs:15000}).then(function(r){
        return r.json().catch(function(){return {}}).then(function(j){return {status:r.status,j:j||{}}});
      }).then(function(x){
        if(seq!==request)throw Error('отменено');
        if(x.status===202||x.j.pending){
          var kb=Math.round((+x.j.read||0)/1024),tb=Math.round((+x.j.total||0)/1024);
          status('читаю день: '+kb+' КБ'+(tb?' из '+tb:''));
          if(Date.now()>=until)throw Error('плата слишком долго читает день');
          return delay(700).then(attempt);
        }
        if(x.status>=500){var e=Error(x.j.err||('HTTP '+x.status));e.retryable=true;throw e}
        if(x.j.ok===false)throw Error(x.j.err||('HTTP '+x.status));
        return x.j;
      }).catch(function(error){
        if(seq!==request)throw error;
        var retry=error.retryable||error.name==='TypeError'||error.name==='AbortError';
        if(Date.now()<until&&retry&&fails<6){fails++;return delay(600*fails).then(attempt)}
        throw error;
      });
    }
    return attempt();
  }

  /* Плата отдаёт максимум 31 день за запрос — длинный диапазон режем на части. */
  function rangeRows(from,to,seq){
    var chunks=[],cur=from;
    while(cur<=to){var end=add(cur,30);if(end>to)end=to;chunks.push([cur,end]);cur=add(end,1)}
    var all=[],i=0;
    function step(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      if(i>=chunks.length)return Promise.resolve(all);
      var c=chunks[i],n=++i;
      return rangeRequest('/range?from='+c[0]+'&to='+c[1],seq,function(j){
        status(progressText(j)+(chunks.length>1?' · часть '+n+' из '+chunks.length:''));
      }).then(function(j){all=all.concat(j.rows||[]);return step()});
    }
    return step();
  }

  /* Загрузка: d — конец периода (обычно выбранная дата), fromDate — начало
     для режима диапазона (пресеты и «свой диапазон»). */
  function load(d,newMode,fromDate){
    if(!isDate(d))return;
    everLoaded=true;
    var prevDate=date,prevMode=mode,prevRange=rangeFrom,prevRangeMode=rangeMode,prevSel=selected;
    if(controller)controller.abort();controller=new AbortController();
    rangeMode=(newMode==='range');
    var from;
    if(rangeMode){
      if(isDate(fromDate||''))rangeFrom=fromDate;
      if(!isDate(rangeFrom)||rangeFrom>d)rangeFrom=d;
      from=rangeFrom;
      mode=autoMode(from,d);
    }else{
      mode=newMode||mode;
      rangeFrom='';
      from=mode==='hours'?d:(mode==='weeks'?add(d,-27):add(d,-6));
    }
    var keepSel=(prevDate===d&&prevMode===mode&&prevRangeMode===rangeMode&&(!rangeMode||prevRange===rangeFrom))?prevSel:-1;
    date=d;selected=-1;points=[];loading=true;var seq=++request;
    plot.innerHTML='';plot.style.display='none';plot.setAttribute('aria-busy','true');
    root.innerHTML='';G('historyDrill').hidden=true;
    G('historyDate').value=d;
    host.querySelectorAll('[data-mode]').forEach(function(b){var on=b.dataset.mode===mode;b.classList.toggle('on',on);b.setAttribute('aria-pressed',on?'true':'false')});
    /* «свой диапазон» всегда показывает текущий период: в часовом виде — дату */
    if(mode==='hours'){var f0=G('rngFrom'),t0=G('rngTo');if(f0)f0.value=d;if(t0)t0.value=d}
    else{var fIn=G('rngFrom'),tIn=G('rngTo');if(fIn)fIn.value=from;if(tIn)tIn.value=d}
    hint('загружаю…');
    G('historyChartTitle').textContent=modeTitle()+' · '+(mode==='hours'?dShort(d):periodText(from,d));
    var jobs;
    if(mode==='hours'){
      status('читаю историю дня…');
      jobs=historyRequest(d,seq);
    }else{
      status('считаю '+(mode==='weeks'?'по неделям':'по дням')+'…');
      jobs=rangeRows(from,d,seq);
    }
    jobs.then(function(j){
      if(seq!==request)return;
      loading=false;plot.setAttribute('aria-busy','false');plot.style.display='block';
      loadedFrom=mode==='hours'?'':from;loadedTo=d;loadTries=0;
      if(mode==='hours'){
        S.date=d;S.hist=j;
        if(window.drawNav)drawNav();if(window.renderDetails)renderDetails(j);
        points=pointsForHours(d,j);
      }else{
        /* …ниже, после сборки точек, выбираем последний день автоматически */
        var rows=j||[];
        points=pointsForPeriod(from,d,mode,rows);
        /* Итоги и таблица периода — в app.js (rngFill), без второго /range. */
        if(window.rngFill)window.rngFill(from,d,rows);
      }
      /* Повторное обновление той же даты/вида не сбрасывает выбор (даже если
         выбрана пустая ячейка). Для нового периода — последний час/день с данными. */
      if(keepSel>=0&&keepSel<points.length)selected=keepSel;
      if(selected<0){var last=-1;for(var q=0;q<points.length;q++)if(points[q].valid)last=q;selected=last}
      render();
    }).catch(function(error){
      if(seq!==request)return;
      loading=false;plot.style.display='none';plot.setAttribute('aria-busy','false');
      status('Не удалось загрузить данные. '+(error&&error.message?error.message:''));
      hint('ошибка загрузки');
      plot.innerHTML='';root.innerHTML='';G('historyDrill').hidden=true;
      everLoaded=false;
    });
  }

  /* Итоги выбранного дня — из 24 часов, чтобы в часовом виде было видно
     весь диапазон, а не только выбранный час. Только известные значения:
     null не превращается в 0, а средняя мощность взвешивается по n. */
  function dayTotalsFor(pts){
    if(!pts||!pts.length)return '';
    var kwh=null,kwhN=0,cnt=0,pmax=null,vmin=null,vmax=null,tmin=null,tmax=null;
    var pW=0,pWN=0,pPlain=0,pPlainN=0;
    pts.forEach(function(pt){
      var v=(pt&&pt.values)||{};
      if(!pt||!pt.valid)return;
      cnt++;
      if(isNum(v.kwh)){kwh=addNum(kwh,v.kwh);kwhN++}
      if(isNum(v.pmax)&&(pmax==null||+v.pmax>pmax))pmax=+v.pmax;
      if(isNum(v.p)){
        var n=+v.n;
        if(n>0){pW+=+v.p*n;pWN+=n}else{pPlain+=+v.p;pPlainN++}
      }
      if(isNum(v.v)&&+v.v>50){vmin=vmin==null?+v.v:Math.min(vmin,+v.v);vmax=vmax==null?+v.v:Math.max(vmax,+v.v)}
      if(isNum(v.t)&&+v.t>-60){tmin=tmin==null?+v.t:Math.min(tmin,+v.t);tmax=tmax==null?+v.t:Math.max(tmax,+v.t)}
    });
    if(!cnt)return 'за день: нет измерений';
    var s='за день: '+(kwh==null?'—':(kwhN<cnt?'≥ ':'')+num(kwh,3)+' кВт·ч');
    if(pWN)s+=' · средняя мощность '+num(pW/pWN,0)+' Вт (взвешено по n часов)';
    else if(pPlainN)s+=' · среднее по доступным часам '+num(pPlain/pPlainN,0)+' Вт';
    if(pmax!=null)s+=' · пик '+num(pmax,0)+' Вт';
    if(vmin!=null)s+=' · U '+pair(vmin,vmax,'В')+' (средние за час, '+cnt+' ч)';
    if(tmin!=null)s+=' · t '+pair(tmin,tmax,'°C')+' (средние за час, '+cnt+' ч)';
    s+=' · часов с измерениями '+cnt+' из 24'+(cnt<24?' — день неполный':'');
    if(pWN&&pPlainN)s+=' · '+pPlainN+' ч без n в среднюю не вошли';
    return s;
  }
  function dayTotalsText(){
    if(mode!=='hours'||!points.length)return '';
    return dayTotalsFor(points);
  }

  /* Осмысленная подпись для каждой ячейки графика: дата, диапазон, значения. */
  function describeItem(item){
    var specs=mode==='hours'?hourly:period,parts=[];
    if(item.valid){
      specs.forEach(function(m){var val=item.values?item.values[m[0]]:null;if(isNum(val))parts.push(m[1]+' '+num(val,m[3])+' '+m[2])});
      if(!parts.length)parts.push('показатели пусты');
      if(item.partial){var why=partialReason(item);parts.push('неполные данные'+(why?': '+why:''))}
    }else parts.push('нет измерений');
    var head=mode==='hours'?(dShort(item.date)+', '+hourRange(item)):periodText(item.date,item.end||item.date);
    return head+': '+parts.join(', ');
  }
  /* Ровно один элемент в tab-порядке (roving tabindex); остальные — стрелками. */
  function paintSelection(){
    plot.querySelectorAll('[data-selection]').forEach(function(r){r.setAttribute('opacity',+r.dataset.selection===selected?'.12':'0')});
    plot.querySelectorAll('[data-index]').forEach(function(g){
      var i=+g.dataset.index,on=i===selected;
      g.setAttribute('aria-selected',on?'true':'false');
      g.setAttribute('tabindex',(on||(selected<0&&i===0))?'0':'-1');
    });
  }

  function render(selectionOnly){
    if(loading)return;
    /* История может обновиться извне (app.js отдаёт /history опросом) — если
       выбор не задан, показываем последний час/день с данными. */
    if(selected<0){for(var q=points.length-1;q>=0;q--)if(points[q]&&points[q].valid){selected=q;break}}
    var specs=mode==='hours'?hourly:period,p=points[selected],v=p?p.values:{};
    var partial=!!(p&&p.valid&&p.partial),why=partial?partialReason(p):'';
    host.classList.toggle('hours-mode',mode==='hours');
    var dtEl=G('historyDayTotals');
    if(dtEl)dtEl.textContent=progressFreeDayTotals();
    hint(modeTitle().replace('Показатели ','')+' · '+(mode==='hours'?(p?p.date:date):(loadedFrom?loadedFrom+' — '+loadedTo:loadedTo||date)));
    G('historyDrill').hidden=mode==='hours'||!p;G('historyDrill').textContent=mode==='weeks'?'Открыть дни недели':'Открыть часы дня';
    plot.setAttribute('aria-label',mode==='hours'?'Показатели по часам, 24 ячейки: стрелки ← → переключают час, Enter выбирает':'Показатели по '+(mode==='weeks'?'неделям':'дням')+': стрелки ← → переключают, Enter выбирает');
    var where,state;
    if(mode==='hours'){where=p?p.date+' · '+hourRange(p):date;state=!p?'выберите час':(p.valid?'данные есть':'нет измерений')}
    else{where=p?p.date+(p.end!==p.date?' — '+p.end:''):'выберите '+(mode==='weeks'?'неделю':'день');state=p?(!p.valid?'нет измерений':(partial?'неполные данные':'данные есть')):''}
    G('historySummary').textContent=where+(state?' · '+state:'')+(partial&&why?' ('+why+')':'');
    legend.innerHTML=specs.map(function(m){
      return '<button type="button" data-series="'+m[0]+'" aria-pressed="'+(!hidden[m[0]])+'"><i style="background:'+colors[m[0]]+'"></i>'+m[1]+' ('+m[2]+')</button>'
    }).join('');
    /* Неполные суммы показываем как «≥», а не как точный итог: null/пропуски
       не должны выглядеть нулём. 0 остаётся настоящим нулём. */
    var pre=partial?'≥ ':'',cards;
    if(mode==='hours'){
      cards=hourly.map(function(m){
        var val=p&&p.valid?v[m[0]]:null;
        return {t:m[1],v:isNum(val)?num(val,m[3])+' '+m[2]:'—'};
      });
    }else{
      cards=[
        {t:'Расход',v:p&&p.valid&&isNum(v.kwh)?pre+num(v.kwh,3)+' кВт·ч':'—',bound:!!partial},
        {t:'Стоимость',v:p&&p.valid&&isNum(v.cost)?pre+num(v.cost,2)+' ₽':'—',bound:!!partial},
        {t:'Пик мощности',v:p&&p.valid&&isNum(v.pmax)?pre+num(v.pmax,0)+' Вт':'—',bound:!!partial},
        {t:'Напряжение мин–макс',v:p&&p.valid?pair(v.vmin,v.vmax,'В'):'—'},
        {t:'Температура реле мин–макс',v:p&&p.valid?pair(v.tmin,v.tmax,'°C'):'—'}
      ];
    }
    root.innerHTML=cards.map(function(c){return '<article class="history-metric'+(c.bound?' partial':'')+'"><div class="head"><strong>'+c.t+'</strong></div><div class="history-value">'+c.v+'</div></article>'}).join('');
    if(partial&&mode!=='hours')root.innerHTML+='<p class="hist-note" role="note">неполные данные ('+why+'): суммы и пик — минимальные (≥), напряжение и температура — только по доступным суткам.</p>';
    if(selectionOnly){paintSelection();return}
    var hadFocus=plot.contains(document.activeElement);
    var mx=specs.map(function(m){
      var values=points.filter(function(x){return x.valid&&isNum(x.values[m[0]])}).map(function(x){return Math.abs(+x.values[m[0]])});
      return Math.max.apply(null,values)||1;
    });
    var left=40,slot=920/Math.max(1,points.length),baseY=218,s='';
    points.forEach(function(item,index){
      var x=left+index*slot,w=Math.min(7,(slot-6)/specs.length),sel=selected===index;
      s+='<g data-index="'+index+'" role="option" aria-selected="'+(sel?'true':'false')+'" tabindex="'+(sel||(selected<0&&index===0)?'0':'-1')+'" aria-label="'+escAttr(describeItem(item))+'">';
      s+='<rect data-selection="'+index+'" x="'+x+'" y="12" width="'+slot+'" height="208" fill="var(--ink)" opacity="'+(sel?'.12':'0')+'"/>';
      specs.forEach(function(m,i){
        if(hidden[m[0]]||!item.valid||!isNum(item.values[m[0]]))return;
        var height=Math.abs(+item.values[m[0]])/mx[i]*188;
        s+='<rect x="'+(x+slot/2+(i-specs.length/2)*w)+'" y="'+(baseY-height)+'" width="'+Math.max(2,w-1)+'" height="'+Math.max(1,height)+'" fill="'+colors[m[0]]+'"><title>'+m[1]+': '+num(item.values[m[0]],m[3])+' '+m[2]+'</title></rect>';
      });
      var vals=item.valid?specs.filter(function(m){return isNum(item.values[m[0]])}).map(function(m){return m[1]+' '+num(item.values[m[0]],m[3])+' '+m[2]}):[];
      var tip=item.label+': '+(item.valid?(vals.length?vals.join(' · '):'показатели пусты')+(item.partial?' · неполные данные':''):'нет измерений');
      s+='<rect x="'+x+'" y="10" width="'+slot+'" height="210" fill="transparent"><title>'+tip+'</title></rect>';
      if(points.length<=7||index%3===0)s+='<text x="'+(x+slot/2)+'" y="244" text-anchor="middle" fill="var(--mut)" font-size="12">'+item.label+'</text>';
      s+='</g>';
    });
    plot.innerHTML=s;
    paintSelection();
    /* После перерисовки возвращаем фокус выбранной ячейке, чтобы клавиатура
       продолжала работать, а выбор не «уезжал» на первый столбик. */
    if(hadFocus&&selected>=0){var fg=plot.querySelector('[data-index="'+selected+'"]');if(fg&&fg.focus){try{fg.focus()}catch(e){}}}
  }

  function progressFreeDayTotals(){try{return dayTotalsText()}catch(e){return ''}}

  function refresh(){
    if(!everLoaded)return;
    load(date||G('historyDate').value||S.date||S.today,mode,rangeMode?rangeFrom:undefined);
  }
  function ensureLoaded(){
    if(everLoaded)return false;
    var d=date||G('historyDate').value||S.date||S.today||BOOT_DATE||'';
    if(!isDate(d)){
      status('жду список дней от платы…');
      if(++loadTries<=120)setTimeout(ensureLoaded,500);   // до ~60 с: первый /data после OTA бывает медленным
      return false;
    }
    load(d,'hours');
    return true;
  }

  window.historyRefresh=refresh;
  window.historyEnsureLoaded=ensureLoaded;
  window.historyLoadRange=function(from,to){
    if(!isDate(from)||!isDate(to))return;
    if(from>to){var x=from;from=to;to=x}
    G('historyDate').value=to;
    load(to,'range',from);
  };
  window.historyAccept=function(j){
    if(!j||!j.date)return;
    if(!everLoaded||mode!=='hours'||rangeMode)return;
    date=j.date;G('historyDate').value=j.date;loadedFrom='';loadedTo=j.date;
    points=pointsForHours(j.date,j);   /* выбор (даже пустого часа) сохраняем */
    render();
  };
  window.openMetricHistory=function(key,d){
    load(d||S.date||S.today,'hours');
    host.scrollIntoView({block:'start'});
  };

  function groupIndex(ev){
    var t=ev.target;if(!t||!t.closest)return -1;
    var g=t.closest('[data-index]');return g?+g.dataset.index:-1;
  }
  function selectAt(i,focus){
    if(i<0||i>=points.length)return;
    selected=i;render(true);
    if(focus){var el=plot.querySelector('[data-index="'+i+'"]');if(el&&el.focus){try{el.focus()}catch(e){}}}
  }
  /* Любая ячейка (в том числе пустая) выбирается кликом и с клавиатуры:
     ←/→ (и ↑/↓) ходят по 24 часам, Home/End — крайние, Enter/пробел — выбор. */
  function select(ev){
    if(loading)return;
    var gi=groupIndex(ev),base=gi>=0?gi:(selected>=0?selected:0),key=ev.type==='keydown'?ev.key:'';
    if(ev.type==='keydown'){
      if(key==='ArrowLeft'||key==='ArrowUp'){ev.preventDefault();selectAt(base-1,true);return}
      if(key==='ArrowRight'||key==='ArrowDown'){ev.preventDefault();selectAt(base+1,true);return}
      if(key==='Home'){ev.preventDefault();selectAt(0,true);return}
      if(key==='End'){ev.preventDefault();selectAt(points.length-1,true);return}
      if(key!=='Enter'&&key!==' '&&key!=='Spacebar')return;
      if(gi<0)return;
      ev.preventDefault();selectAt(gi,true);return;
    }
    if(gi<0)return;
    ev.preventDefault();selectAt(gi,false);
  }
  plot.addEventListener('click',select);
  plot.addEventListener('keydown',select);
  plot.addEventListener('dblclick',function(ev){
    if(loading)return;
    var gi=groupIndex(ev);if(gi<0)return;
    var p=points[gi];if(!p)return;
    if(mode==='weeks')load(p.end,'days');else load(p.date,'hours');
  });
  legend.addEventListener('click',function(ev){
    var b=ev.target.closest('[data-series]');if(!b)return;
    var key=b.dataset.series;hidden[key]=!hidden[key];render();
    legend.querySelector('[data-series="'+key+'"]').focus();
  });
  /* Ручной вид: Часы / Дни / Недели — для текущего периода или выбранной даты */
  G('historyModes').addEventListener('click',function(ev){
    var b=ev.target.closest('[data-mode]');if(!b)return;
    var want=b.dataset.mode;
    var d=G('historyDate').value||date||S.date||S.today;
    if(want===mode)return;
    if(rangeMode){
      if(want==='hours')load(d,'hours');else load(d,want,rangeFrom);
    }else{
      load(d,want);
    }
  });
  G('historyDrill').onclick=function(){
    var p=points[selected];if(!p)return;
    if(mode==='weeks')load(p.end,'days');else load(p.date,'hours');
  };
  G('historyReload').onclick=function(){
    load(G('historyDate').value||date,mode,rangeMode?rangeFrom:'');
  };
  /* Блок открыт всегда — грузим сразу, без ожидания клика по summary */
  host.addEventListener('toggle',function(){if(host.open)ensureLoaded()});
  if(host.open)ensureLoaded();
  [['dVbig','v'],['dTbig','t'],['mp','p'],['tkwh','kwh'],['pe','kwh']].forEach(function(pair){
    var el=G(pair[0]);if(!el)return;
    var card=el.closest('.card,.t');card.style.cursor='pointer';card.tabIndex=0;card.setAttribute('role','button');
    card.addEventListener('click',function(){window.openMetricHistory(pair[1])});
    card.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();window.openMetricHistory(pair[1])}});
  });
  var initial=S.date||S.today||BOOT_DATE||'';
  if(isDate(initial)){date=initial;G('historyDate').value=initial}
  hint('сегодня · по часам');
})();

/* ---------- P1.2: источник и свежесть показателей, качество данных ----------
   /measure отдаёт по КАЖДОМУ показателю отдельно локальное реле и облако:
   {value,unit,source:'local|cloud|none',ageSec,valid,local:{value,ageSec},
    cloud:{...},ttlSec}. /storage добавляет здоровье записи на карту.
   Здесь только отображение; опрос идёт из существующего цикла poll
   (window.domStatusTick вызывает app.js) — своего таймера нет.
   /measure — не чаще 5 с, /storage — раз в минуту (и 20 с после ошибки).
   При 503/таймауте значения не обнуляются: остаются последние известные,
   а в строке состояния честно пишется «нет ответа /measure». */
(function(){
  'use strict';
  var MEAS={data:null,at:0,tryAt:0,tried:false,err:0};
  var SD={data:null,at:0,tryAt:0,failAt:0,tried:false,err:0};
  var DLAST=null,DAT=0,DOk=false;
  var MEAS_EVERY=5000,MEAS_STALE=15000,SD_EVERY=30000,SD_RETRY=15000;
  /* [префикс, ключ /measure, подпись, единица, знаков после запятой] */
  var METRICS=[['V','voltage','Напряжение','В',0],['P','power','Мощность','Вт',0],
               ['I','current','Ток','А',2],['T','temperature','Темп. реле','°C',1]];
  /* [ключ /measure, id «получено…», id большого числа, знаков, единица] */
  var CARDS=[['voltage','dVnow','dVbig',0,'В'],['current','dAnow','dAbig',2,'А'],
             ['temperature','dTnow','dTbig',1,'°C']];
  var WHY={0:'ок',1:'нет Wi-Fi',2:'нет интернета',3:'нет токена облака',
           4:'ошибка запроса к облаку',5:'облако ответило отказом',
           6:'в ответе облака нет напряжения'};
  function now(){return Date.now()}
  function ageTxt(sec){sec=Math.max(0,Math.round(+sec||0));return sec<90?sec+' с':Math.round(sec/60)+' мин'}
  function drift(){return MEAS.data?Math.max(0,(now()-MEAS.at)/1000):0}
  function candAge(m){var a=[];
    if(m.local&&m.local.value!=null)a.push(+m.local.ageSec||0);
    if(m.cloud&&m.cloud.value!=null)a.push(+m.cloud.ageSec||0);
    return a.length?Math.min.apply(null,a):null}
  /* возраст последнего известного значения, даже если оно уже вне TTL */
  function effAge(m){if(!m)return null;var a=m.source==='none'?candAge(m):(+m.ageSec||0);return a==null?null:a+drift()}
  /* источник мог истечь, пока не было ответа /measure — проверяем TTL сами */
  function effSrc(m){if(!m||m.source==='none')return 'none';
    var a=(+m.ageSec||0)+drift(),ttl=+m.ttlSec||0;return ttl>0&&a>ttl?'none':m.source}
  function numOr(v,n){return v==null||isNaN(+v)?'—':num(+v,n)}
  function gb(b){if(b==null||isNaN(+b))return '—';var g=+b/1073741824;
    return g>=1?g.toFixed(1).replace('.',',')+' ГБ':Math.round(+b/1048576)+' МБ'}
  /* freeBytes может быть неверным (старые сборки отдавали дубль ключа и
     браузер брал 1,2 ГБ вместо 117). Если total и used известны — сверяем. */
  function freeBytesSafe(st){
    if(!st)return null;
    var total=+st.totalBytes,used=+st.usedBytes,free=+st.freeBytes;
    var calc=(total>0&&used>=0)?(total-used):null;
    if(calc!=null&&(st.freeBytes==null||isNaN(free)||Math.abs(free-calc)>1048576))return calc;
    return isNaN(free)?calc:free;
  }
  function setRow(id,t,cls){var el=G(id);if(!el)return;el.textContent=t;el.className=cls||''}

  /* карточки «Сейчас»: источник и возраст, при source=none — не ноль */
  function renderCards(){
    var md=MEAS.data;if(!md)return;
    for(var i=0;i<CARDS.length;i++){
      var c=CARDS[i],m=md[c[0]];if(!m)continue;
      var big=G(c[2]),el=G(c[1]);if(!big||!el)continue;
      var s=effSrc(m),a=effAge(m),label;
      if(s==='local'||s==='cloud'){
        big.textContent=numOr(m.value,c[3]);big.className='';
        label=(s==='local'?'локальное реле':'облако')+' · '+ageTxt(a);
      }else{
        big.textContent='—';big.className='none';
        label=a==null?'нет измерений':'нет свежих данных · устарело '+ageTxt(a);
      }
      el.textContent=label;
      var lv=m.local&&m.local.value!=null?numOr(m.local.value,c[3])+' '+c[4]+' ('+ageTxt((+m.local.ageSec||0)+drift())+')':'нет';
      var cv=m.cloud&&m.cloud.value!=null?numOr(m.cloud.value,c[3])+' '+c[4]+' ('+ageTxt((+m.cloud.ageSec||0)+drift())+')':'нет';
      big.title='локально: '+lv+'; облако: '+cv;
      el.title=big.title;
    }
  }
  /* компактная строка: группируем показатели по источнику */
  function summary(){
    var md=MEAS.data;if(!md)return '';
    var groups={},order=[];
    for(var i=0;i<METRICS.length;i++){
      var m=md[METRICS[i][1]];if(!m)continue;
      var s=effSrc(m);
      if(!groups[s]){groups[s]={names:[],age:-1};order.push(s)}
      groups[s].names.push(METRICS[i][0]);
      var a=effAge(m);if(a!=null&&a>=0&&a>groups[s].age)groups[s].age=a;
    }
    var out=[];
    for(var j=0;j<order.length;j++){
      var k=order[j],g=groups[k],pre=g.names.join('/')+' — ';
      if(k==='none')out.push(pre+(g.age<0?'нет измерений':'устарело · '+ageTxt(g.age)));
      else out.push(pre+(k==='local'?'локальное реле':'облако')+' · '+ageTxt(Math.max(0,g.age)));
    }
    return out.join(' · ');
  }
  /* LAN и WAN раздельно: плата отвечает сама, облако — через интернет.
     Плата может ответить и только на /measure (если /data занят) — это честно. */
  function measFresh(){return !!MEAS.data&&now()-MEAS.at<MEAS_STALE}
  function lanAlive(){return (DOk&&DAT&&now()-DAT<MEAS_STALE)||measFresh()}
  function boardRow(){
    // ВАЖНО: подпись строки уже «Плата отвечает», поэтому значение не должно
    // начинаться с «отвечает» — иначе получается «Плата отвечает отвечает …».
    if(DOk&&DAT&&now()-DAT<MEAS_STALE)return {t:'да · /data '+ageTxt((now()-DAT)/1000)+' назад',cls:''};
    if(measFresh())return {t:'только /measure'+(DAT?' · /data '+ageTxt((now()-DAT)/1000)+' назад':''),cls:'bad'};
    if(DAT)return {t:'нет · последний ответ '+ageTxt((now()-DAT)/1000)+' назад',cls:'err'};
    return {t:'ожидание',cls:''};
  }
  function linkTxt(){
    var s='плата: '+boardRow().t;
    var d=DLAST;
    if(!lanAlive())return s+' · облако: нет свежих данных платы';
    if(!d)return s+' · облако: нет данных';
    var ca=(+d.cAge||0)+Math.max(0,(now()-DAT)/1000),wan;
    if(!d.net)wan='нет интернета';
    else if(+d.cFail>0)wan='ошибка · '+(WHY[+d.cWhy]||('код '+d.cWhy));
    else if(+d.cOk>0)wan='отвечает · '+ageTxt(ca);
    else wan='нет успешных ответов';
    return s+' · облако: '+wan;
  }
  function renderPanel(){
    var t=now(),md=MEAS.data,d=DLAST,lan=G('sbLan'),br=boardRow();
    if(lan)lan.textContent=lanAlive()?('LAN · плата отвечает'+(!DOk||t-DAT>=MEAS_STALE?' по /measure':' · /data '+ageTxt((t-DAT)/1000)+' назад'))
      :('LAN · плата не отвечает'+(DAT?' · последние данные '+ageTxt((t-DAT)/1000)+' назад':''));
    setRow('sbBoard',br.t,br.cls);
    setRow('sbRssi',d&&d.rssi!=null?d.rssi+' дБм':'—',d&&d.rssi!=null&&+d.rssi<=-75?'bad':'');
    setRow('sbRelay',d?(d.r?'ВКЛ':'ВЫКЛ'):'—','');
    setRow('sbClock',md?(md.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно (/measure)',md&&!md.clockOk?'bad':'');
    /* SD: /storage отдаёт ok, freeBytes, writeFail, writeFailRun, lastWriteAgeSec, archiveError */
    if(!SD.tried){setRow('sbSd','ожидание /storage','');setRow('sbSdFree','—','');setRow('sbSdWrite','—','')}
    else if(SD.data){
      var as=(t-SD.at)/1000,wf=+SD.data.writeFail||0,wr=+SD.data.writeFailRun||0,
          failed=SD.failAt>SD.at;
      setRow('sbSd',failed?('нет ответа /storage · данные '+ageTxt(as)+' назад')
        :('в порядке'+(as>120?' · проверено '+ageTxt(as)+' назад':'')),failed?'err':'');
      var free=freeBytesSafe(SD.data);
      setRow('sbSdFree',gb(free)+' свободно',free===null?'err':'');
      var ws=wf===0?'ошибок нет':'ошибок '+wf+' всего'+(wr?' · подряд '+wr:'');
      if(+SD.data.lastWriteAgeSec>0)ws+=' · последняя запись '+ageTxt(+SD.data.lastWriteAgeSec+as)+' назад';
      if(SD.data.archiveError)ws+=' · ошибка архива';
      if(SD.data.lowSpace)ws+=' · мало места';
      setRow('sbSdWrite',ws,(wf>0||SD.data.archiveError||SD.data.lowSpace)?'bad':'');
    }else{
      setRow('sbSd',SD.err===503?'карта не готова (503)':'нет ответа /storage','err');
      setRow('sbSdFree','—','');setRow('sbSdWrite','—','');
    }
    if(!d){setRow('sbNet','—','');setRow('sbCOk','—','');setRow('sbCFail','—','');setRow('sbCAge','—','');setRow('sbCWhy','—','')}
    else{
      setRow('sbNet',d.net?'проверка прошла':'проверка не прошла',d.net?'':'bad');
      setRow('sbCOk',(+d.cOk||0)+' из '+(+d.cTry||0)+' попыток','');
      setRow('sbCFail',(+d.cFail||0)+' подряд',(+d.cFail>0)?'bad':'');
      setRow('sbCAge','попытка '+ageTxt((+d.cAge||0)+Math.max(0,(t-DAT)/1000))+' назад','');
      // cHold: 1 — облако на паузе (напряжение свежее от локального реле),
      // 8 — кончился сетевой бюджет. Без этого панель показывала старую
      // причину «нет токена» и выглядело, будто токен пропал навсегда.
      var w=+d.cWhy||0,why=(WHY[w]||('код '+w)),whyCls=(w?'bad':'');
      if(+d.cHold===1){why='пауза: напряжение свежее (локальное реле)';whyCls=''}
      else if(+d.cHold===8){why='пауза: сетевой бюджет на этот проход';whyCls='bad'}
      if(+d.tok===0&&!w&&+d.cHold!==1&&+d.cHold!==8)why='токена ещё нет';
      setRow('sbCWhy',why,whyCls);
    }
    var sm=G('sbMeasure');
    if(sm){
      if(md)sm.textContent=(t-MEAS.at<MEAS_STALE?'ответ ':'нет ответа · последний ')+ageTxt((t-MEAS.at)/1000)+' назад';
      else sm.textContent=MEAS.tried?'нет ответа':'ожидание';
    }
  }
  function candCell(c,unit,dec){
    return !c||c.value==null?'—':numOr(c.value,dec)+' '+unit+' · '+ageTxt((+c.ageSec||0)+drift());
  }
  function renderFresh(){
    var tb=G('sbFresh');if(!tb)return;
    var md=MEAS.data,htm='';
    if(!md){
      var tries=MEAS.tried?(now()-MEAS.tryAt)/1000:0;
      htm='<tr><td colspan="7" class="mut">'+(MEAS.tried?'нет ответа /measure · '+(tries<5?'последняя попытка только что':'попытка '+ageTxt(tries)+' назад'):'ожидание ответа /measure')+'</td></tr>';
    }else{
      for(var i=0;i<METRICS.length;i++){
        var it=METRICS[i],m=md[it[1]];if(!m)continue;
        var s=effSrc(m),a=effAge(m),age;
        if(s==='none')age=a==null?'нет измерений':'устарело · '+ageTxt(a);else age=ageTxt(a);
        var st=s==='local'?'локальное реле':s==='cloud'?'облако':'не определён';
        htm+='<tr><td>'+it[2]+'</td><td'+(s==='none'?' class="src-bad"':'')+'>'+st+
             '</td><td>'+age+'</td><td>'+(s==='none'?'—':numOr(m.value,it[4])+' '+it[3])+
             '</td><td>'+candCell(m.local,it[3],it[4])+'</td><td>'+candCell(m.cloud,it[3],it[4])+
             '</td><td>'+(m.ttlSec?m.ttlSec+' с':'—')+'</td></tr>';
      }
    }
    tb.innerHTML=htm;
  }
  function renderAll(){
    var live=G('statLive'),link=G('statLink');
    if(live){
      var s=summary();
      live.textContent='источник и свежесть: '+(s||(MEAS.tried?'нет ответа /measure':'ожидание /measure'))+
        (!MEAS.data||now()-MEAS.at<MEAS_STALE?'':' · нет ответа /measure');
    }
    if(link)link.textContent=linkTxt();
    renderCards();renderPanel();renderFresh();
  }
  function fetchMeasure(){
    var t=now();if(t-MEAS.tryAt<MEAS_EVERY)return;
    MEAS.tryAt=t;MEAS.tried=true;
    fetch('/measure',{cache:'no-store',timeoutMs:4000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return r.json();
    }).then(function(j){
      if(!j||typeof j!=='object'||!j.voltage)throw Error('bad /measure');
      MEAS.data=j;MEAS.at=now();MEAS.err=0;renderAll();
    }).catch(function(e){
      MEAS.err=(e&&e.code)||0;renderAll();   /* значения не трогаем — «не ноль» */
    });
  }
  function fetchStorage(){
    var t=now(),wait=(SD.data&&SD.failAt<=SD.at)?SD_EVERY:SD_RETRY;
    if(t-SD.tryAt<wait)return;
    SD.tryAt=t;SD.tried=true;
    fetch('/storage',{cache:'no-store',timeoutMs:5000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return r.json();
    }).then(function(j){
      SD.data=j&&j.ok?j:null;SD.at=now();SD.err=0;renderAll();
    }).catch(function(e){
      SD.err=(e&&e.code)||0;SD.failAt=now();renderAll();
    });
  }
  /* вызывается из цикла poll в app.js: d — свежий /data или null при ошибке */
  window.domP12={snapshot:function(){return {meas:MEAS.data,measAt:MEAS.at,measTried:MEAS.tried,measErr:MEAS.err,sd:SD.data,sdAt:SD.at,sdTried:SD.tried,sdErr:SD.err,data:DLAST,dataAt:DAT,dataOk:DOk}},
    age:ageTxt,gb:gb,src:effSrc,ageOf:effAge};
  window.domStatusTick=function(d){
    if(d){DLAST=d;DAT=now();DOk=true}else{DOk=false}
    // Первый успешный /data даёт точную дату: если авто-загрузка истории
    // не успела (блок открыт), грузим сейчас — без клика по summary.
    // ensureLoaded — из первого IIFE, наружу отдан как window.historyEnsureLoaded.
    if(d&&window.historyEnsureLoaded&&G('historyWorkspace')&&G('historyWorkspace').open)window.historyEnsureLoaded();
    fetchMeasure();fetchStorage();renderAll();
  };
})();
/* ---------- P1.3/P1.5: события, диагностика, обезличенный отчёт ----------
   Источники: /vlog, /boots, /clog, /gapfill/status, /data; V/P/T/I и LAN/WAN —
   из панели P1.2 (window.domP12). Опрос из цикла poll: /vlog,/boots,/clog —
   не чаще 10 с, /gapfill/status — 20 с. Отчёт только в <pre> и в буфер по
   кнопке, без IP, MAC, SSID, токенов, device id и значений измерений. */
(function(){
  'use strict';
  var EV_EVERY=10000,GAP_EVERY=20000,OBS=Date.now();
  var NEXT={vlog:0,boots:0,clog:0,gap:0};
  var EV={vlog:null,vlogErr:0,boots:null,bootsErr:0,clog:null,clogErr:0,gap:null,gapErr:0};
  var LASTD=null;
  function mp(s){var o={},a=s.split(';');for(var i=0;i<a.length;i++){var p=a[i].split('=');o[p[0]]=p[1]}return o}
  var STAGE=mp('ok=успешно;clock=нет времени;connect=соединение;print=отправка запроса;hdr=чтение заголовков;http=ответ не HTTP;short=тело короче;empty=пустое тело;budget=бюджет сети;dns=DNS');
  var WHY=mp('0=нет ошибки;1=нет Wi-Fi;2=нет интернета;3=нет токена облака;4=запрос к облаку не выполнен;5=облако ответило отказом;6=в ответе нет напряжения');
  var RR=mp('POWERON=включение питания;EXT_PIN=внешний сигнал;SW_RESET=программный сброс;PANIC=паника;INT_WDT=сторожевой таймер;TASK_WDT=сторожевой таймер задачи;WDT=сторожевой таймер;DEEPSLEEP=глубокий сон;BROWNOUT=просадка питания;SDIO=SDIO;OTA=обновление по Wi-Fi;UNKNOWN=неизвестная причина');
  var GAPST=mp('idle=ожидание;check_rows=проверка строк;prepare=подготовка;check_space=проверка места;backup_rename=резервная копия;rewrite=перезапись файла;check_insert_count=сверка вставок;activate=активация;verify=проверка размера;complete=завершено');
  var GAPCODE=mp('none=нет;running=выполняется;no_cloud_data=в облаке нет записей;sd_unavailable=карта недоступна;recover_old_failed=не вернулся .old;cleanup_old_failed=не убрать .old;cleanup_tmp_failed=не убрать .tmp;target_open_failed=не открылся файл;target_too_small=файл слишком мал;insufficient_space=не хватает места;backup_cleanup_failed=не удалилась .bak;backup_rename_failed=не сделать .bak;rewrite_failed=ошибка перезаписи;insert_count_mismatch=вставки не совпали;activate_failed=не активировать;final_size_mismatch=размер не совпал;success=успешно');
  var MET=['voltage','power','current','temperature'],MKEY=['V','P','I','T'];
  function n(){return Date.now()}
  function p2(v){return (v<10?'0':'')+v}
  function clock(ts){var d=new Date(ts);return p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds())}
  function dt(ts){var d=new Date(ts),t=new Date();
    return d.toDateString()===t.toDateString()?clock(ts):p2(d.getDate())+'.'+p2(d.getMonth()+1)+' '+p2(d.getHours())+':'+p2(d.getMinutes())}
  function plur(k,a,b,c){k=Math.abs(+k||0)%100;var d=k%10;return k>10&&k<20?c:(d>1&&d<5?b:(d===1?a:c))}
  function dur(s){s=Math.max(0,Math.round(+s||0));if(s<90)return s+' с';if(s<5400)return Math.round(s/60)+' мин';
    var h=Math.floor(s/3600),m=Math.round((s%3600)/60);return h+' ч'+(m?' '+m+' мин':'')}
  /* /boots: первая строка — текущая загрузка, далее «дата причина rrn up(прошлая сессия)» */
  function bootsRows(){
    if(!EV.boots)return [];
    var o=[],ls=String(EV.boots).replace(/\r/g,'').split('\n'),i,m;
    for(i=0;i<ls.length;i++){
      m=/^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) rr=([A-Z_a-z0-9]+)\((\d+)\) rrn=(\d+) up=(\d+)/.exec(ls[i].trim());
      if(m)o.push({ts:new Date(+m[1],+m[2]-1,+m[3],+m[4],+m[5],+m[6]).getTime(),name:m[7],up:+m[10]});
    }
    return o;
  }
  /* /clog: строки «uptime_ms stage why http len» + счётчики */
  function clogRows(){
    var o=[],ls=String(EV.clog||'').replace(/\r/g,'').split('\n'),i,m;
    for(i=1;i<ls.length;i++){
      m=/^(\d+)\s+(\S+)\s+(\d+)\s+(-?\d+)\s+(-?\d+)\s*$/.exec(ls[i].trim());
      if(m)o.push({at:+m[1],stage:m[2],why:+m[3],http:+m[4],len:+m[5]});
    }
    return o;
  }
  function clogCounters(){
    var m=/cTry=(\d+) cOk=(\d+) cFail=(\d+) cAge=(\d+)/.exec(String(EV.clog||''));
    return m?{try:+m[1],ok:+m[2],fail:+m[3]}:null;
  }
  function clogLine(x,d){
    var w=d&&+d.up?'за '+dur(Math.max(0,(+d.up||0)-x.at/1000))+' до снимка':'uptime '+Math.round(x.at/1000)+' с';
    return w+' · этап: '+(STAGE[x.stage]||x.stage)+' · причина: '+(WHY[x.why]||('код '+x.why))+' · HTTP '+x.http+' · '+x.len+' Б';
  }
  function gapText(){
    if(!EV.gap)return null;
    var g=EV.gap;
    return {stage:GAPST[g.commitStage]||(g.commitStage||'—'),code:GAPCODE[g.commitCode]||(g.commitCode||'—'),
            noData:g.noData?'да':'нет',msg:g.msg||'',rows:+g.rows||0,active:+g.active||0};
  }
  function measText(key){
    var P=window.domP12;
    if(!P||!P.snapshot)return {t:'нет данных /measure',age:'',cls:''};
    var s=P.snapshot(),m=s.meas&&s.meas[key];
    if(!m)return {t:'нет данных /measure',age:'',cls:''};
    var so=P.src(m),a=P.ageOf(m);
    if(so==='none')return {t:a==null?'нет измерений':'нет свежих данных',age:a==null?'':P.age(a),cls:'bad'};
    return {t:so==='local'?'локальное реле':'облако',age:P.age(a),cls:''};
  }
  function sdText(){
    var P=window.domP12;
    if(!P||!P.snapshot)return null;
    var s=P.snapshot();if(!s.sd)return null;
    var d=s.sd,as=Math.max(0,(n()-s.sdAt)/1000);
    return {fail:+d.writeFail||0,run:+d.writeFailRun||0,age:(+d.lastWriteAgeSec||0)+as,
            arch:!!d.archiveError,low:!!d.lowSpace,free:d.freeBytes};
  }
  /* «События»: только реальные сообщения платы; пусто — «событий нет» */
  function renderEvents(){
    var d=LASTD,rows=[],i,bl=bootsRows(),e;
    if(EV.vlog&&EV.vlog.items){e=EV.vlog.items;
      for(i=0;i<e.length&&i<20;i++)rows.push({ts:(+e[i].t||0)*1000,tag:'напряж.',cls:e[i].k?'swl':'sag',
        text:(e[i].k?'всплеск':'провал')+' '+num(e[i].v)+' В · '+num(e[i].s)+' с'})}
    for(i=0;i<bl.length&&i<20;i++)rows.push({ts:bl[i].ts,tag:'ребут',cls:'reb',
      text:'перезагрузка · '+(RR[bl[i].name]||bl[i].name)+' · прошлая сессия '+dur(bl[i].up)});
    if(d&&+d.cWhy>0)rows.push({ts:n()-(+d.cAge||0)*1000,tag:'облако',cls:'bad',
      text:'облако: '+(WHY[+d.cWhy]||('код '+d.cWhy))+(+d.cFail>1?' · подряд '+d.cFail:'')});
    rows.sort(function(a,b){return b.ts-a.ts});if(rows.length>20)rows.length=20;
    var list=G('evHistList');
    if(list){
      if(!rows.length)list.innerHTML='<div class="ev"><span>'+((!EV.vlog&&!EV.boots&&!d)?'ожидание /vlog, /boots, /data':(EV.vlog?'событий нет':'/vlog: нет ответа'))+'</span></div>';
      else{var h='';for(i=0;i<rows.length;i++)h+='<div class="ev"><span class="t">'+dt(rows[i].ts)+'</span><span class="ev-tag '+rows[i].cls+'">'+rows[i].tag+'</span><span>'+rows[i].text+'</span></div>';list.innerHTML=h}
    }
    var st=G('evState');
    if(st)st.innerHTML=d?('<span><b>старт текущей сессии:</b> '+dt(n()-(+d.up||0)*1000)+' · <i>по uptime /data</i></span><span><b>последняя ошибка облака:</b> '+(+d.cWhy>0?((WHY[+d.cWhy]||('код '+d.cWhy))+(+d.cFail?' · подряд '+d.cFail:'')):'нет')+'</span>'):'<span>ожидание /data</span>';
    var nv=(EV.vlog&&+EV.vlog.n)||0,er=[];
    if(EV.vlogErr)er.push('/vlog: нет ответа');
    if(EV.bootsErr)er.push('/boots: нет ответа');
    txt('evLive','события: '+(rows.length?rows.length+' '+plur(rows.length,'запись','записи','записей'):'пока пусто')+' · /vlog n='+nv+' · загрузок в журнале '+bl.length+(er.length?' · '+er.join(' · '):''));
    txt('evObs','данные наблюдаются с '+dt(OBS));
    txt('evFoot',(er.length?'Не получено: '+er.join(', ')+'. ':'')+'Пока страница закрыта, события напряжения не пишутся; перезагрузки — из /boots.log на карте.');
  }
  /* «Диагностика»: версия/uptime, V/P/T/I, LAN/WAN из /data, SD, gapfill, /clog */
  function renderDiag(){
    var d=LASTD,P=window.domP12,snap=P&&P.snapshot?P.snapshot():null,i,sd,g;
    txt('dgVer',d&&d.fw?d.fw:'—');txt('dgBuild',d&&d.build?d.build:'—');txt('dgUp',d?dur(+d.up||0):'—');
    txt('dgClock',snap&&snap.meas?(snap.meas.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно (/measure)');
    if(!d){txt('dgLan','ожидание /data');txt('dgWan','ожидание /data')}
    else{
      txt('dgLan','плата '+(snap&&snap.dataOk?'отвечает':'не отвечает')+' · Wi-Fi '+(d.rssi!=null?'есть':'нет данных')+' · реле '+(d.r?'ВКЛ':'ВЫКЛ'));
      var cw=+d.cWhy||0,cadd=snap?Math.max(0,(n()-snap.dataAt)/1000):0;
      txt('dgWan','интернет '+(d.net?'есть':'нет')+' · облако '+(+d.cOk||0)+' из '+(+d.cTry||0)+' · подряд ошибок '+(+d.cFail||0)+' · попытка '+dur((+d.cAge||0)+cadd)+' назад'+(cw?' · причина: '+(WHY[cw]||('код '+cw)):''));
    }
    for(i=0;i<MET.length;i++){var r=measText(MET[i]);var el=G('dg'+MKEY[i]);if(el){el.textContent=r.t+(r.age?' · '+r.age:'');el.className=r.cls||''}}
    sd=sdText();
    if(!snap||!snap.sdTried){txt('dgSdFail','ожидание /storage');txt('dgSdAge','—');txt('dgSdArch','—');txt('dgSdFree','—')}
    else if(!sd){txt('dgSdFail',snap.sdErr===503?'карта не готова (503)':'нет ответа /storage');txt('dgSdAge','—');txt('dgSdArch','—');txt('dgSdFree','—')}
    else{txt('dgSdFail','ошибок '+sd.fail+' всего'+(sd.run?' · подряд '+sd.run:''));
      txt('dgSdAge',sd.age>0?'последняя запись '+dur(sd.age)+' назад':'нет данных о записи');
      txt('dgSdArch',sd.arch?'ошибка архива':(sd.low?'мало места':'нет ошибки'));
      txt('dgSdFree',window.domP12.gb(sd.free)+' свободно')}
    g=gapText();
    if(!g){txt('dgGapStage',EV.gapErr?'нет ответа /gapfill/status':'ожидание /gapfill/status');txt('dgGapCode','—');txt('dgGapNo','—');txt('dgGapMsg','—')}
    else{txt('dgGapStage',g.stage+(g.active?' · идёт':''));txt('dgGapCode',g.code);txt('dgGapNo',g.noData+(g.rows?' · строк '+g.rows:''));txt('dgGapMsg',g.msg||'—')}
    var cl=G('dgClog');
    if(cl){
      if(EV.clogErr)cl.textContent='нет ответа /clog';
      else if(!EV.clog)cl.textContent='ожидание /clog';
      else{var rr=clogRows(),cc=clogCounters(),out=[];
        for(i=rr.length-1;i>=0&&out.length<3;i--)out.push(clogLine(rr[i],d));
        if(cc)out.push('всего: cTry='+cc.try+' · cOk='+cc.ok+' · cFail='+cc.fail);
        cl.textContent=out.length?out.join('\n'):'строк нет'}
    }
    txt('dgLive','диагностика: '+(d?'CYD '+(d.fw||'?')+' · uptime '+dur(+d.up||0):'ожидание /data'));
  }
  function renderAll(){renderEvents();renderDiag()}
  /* опрос с ограничением частоты — только из общего цикла poll */
  function load(key,url,isJson){
    var t=n();if(t<NEXT[key])return;NEXT[key]=t+(key==='gap'?GAP_EVERY:EV_EVERY);
    fetch(url,{cache:'no-store',timeoutMs:5000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return isJson?r.json():r.text();
    }).then(function(j){EV[key]=j;EV[key+'Err']=0;renderAll()})
      .catch(function(e){EV[key+'Err']=(e&&e.code)||1;renderAll()});
  }
  function fetchAll(){NEXT.vlog=NEXT.boots=NEXT.clog=NEXT.gap=0;load('vlog','/vlog',true);load('boots','/boots',false);load('clog','/clog',false);load('gap','/gapfill/status',true)}
  function tick(d){
    if(d)LASTD=d;
    load('vlog','/vlog',true);load('boots','/boots',false);load('clog','/clog',false);load('gap','/gapfill/status',true);
    renderAll();
  }
  /* обезличенный отчёт: состояния, коды, возраст, счётчики — без значений */
  function reportLines(){
    var d=LASTD,P=window.domP12,snap=P&&P.snapshot?P.snapshot():null,i,L=[];
    L.push('Дом — энергия · обезличенный отчёт');
    L.push('Сформирован: '+dt(n())+' (время браузера)');
    L.push('Версия: '+(d&&d.fw?d.fw:'—')+' · сборка '+(d&&d.build?d.build:'—'));
    L.push('Uptime платы: '+(d?dur(+d.up||0):'—'));
    L.push('Время платы: '+(snap&&snap.meas?(snap.meas.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно'));
    if(d){
      L.push('Текущая сессия: '+dt(n()-(+d.up||0)*1000)+' (по uptime /data)');
      L.push('LAN: плата отвечает · Wi-Fi '+(d.rssi!=null?'есть':'нет данных')+' · реле '+(d.r?'ВКЛ':'ВЫКЛ'));
      var cw=+d.cWhy||0,ca=(+d.cAge||0)+(snap?Math.max(0,(n()-snap.dataAt)/1000):0);
      L.push('WAN: интернет '+(d.net?'есть':'нет')+' · облако '+(+d.cOk||0)+' из '+(+d.cTry||0)+' попыток · подряд ошибок '+(+d.cFail||0)+' · последняя попытка '+dur(ca)+' назад · причина: '+(cw?(WHY[cw]||('код '+cw)):'нет'));
    }else L.push('Плата: нет свежих /data');
    L.push('Показатели (только источник и возраст, без значений):');
    for(i=0;i<MET.length;i++){var r=measText(MET[i]);L.push('  '+MKEY[i]+' — '+r.t+(r.age&&r.age!=='—'?' · '+r.age:''))}
    var sd=sdText();
    if(sd)L.push('SD: ошибок записи '+sd.fail+(sd.run?' (подряд '+sd.run+')':'')+' · последняя запись '+dur(sd.age)+' назад · архив: '+(sd.arch?'ошибка':'без ошибки')+(sd.low?' · мало места':'')+' · свободно '+window.domP12.gb(sd.free));
    else L.push('SD: нет ответа /storage');
    var g=gapText();
    L.push('GAPFILL: '+(g?('стадия '+g.stage+' · код '+g.code+' · в облаке данных нет: '+g.noData+' · сообщение: '+(g.msg||'—')):'нет ответа /gapfill/status'));
    var bl=bootsRows();
    L.push('Перезагрузки (/boots): '+(bl.length?bl.length+' '+plur(bl.length,'запись','записи','записей')+' · последняя причина: '+(RR[bl[bl.length-1].name]||bl[bl.length-1].name)+' · прошлая сессия '+dur(bl[bl.length-1].up):'журнал пуст или недоступен'));
    var vn=(EV.vlog&&+EV.vlog.n)||0;
    L.push('События напряжения (/vlog): '+vn+(vn?' '+plur(vn,'запись','записи','записей'):' — событий нет'));
    var cr=clogRows(),cc=clogCounters();
    L.push('Облако (/clog), последние '+Math.min(3,cr.length)+':');
    if(!cr.length)L.push('  нет строк / нет ответа');
    for(i=cr.length-1;i>=0&&i>=cr.length-3;i--)L.push('  '+clogLine(cr[i],d));
    if(cc)L.push('Счётчики: cTry='+cc.try+' cOk='+cc.ok+' cFail='+cc.fail);
    L.push('Обезличено: IP, MAC, SSID, токены, device id и значения измерений не включены; отчёт никуда не отправляется.');
    return L.join('\n');
  }
  /* маскировка на случай утечки; build (разрешён) скрываем на время замены */
  function sanitize(s){
    var K='\u0001',build=(LASTD&&LASTD.build)?String(LASTD.build):'';
    if(build)s=s.split(build).join(K+'B'+K);
    s=String(s).replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g,'[IP скрыт]')
      .replace(/\b(?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}\b/g,'[MAC скрыт]')
      .replace(/\b[0-9a-fA-F]{16,}\b/g,'[идентификатор скрыт]')
      .replace(/\b(?:[0-9a-fA-F]{4}:){7}[0-9a-fA-F]{4}\b/g,'[IPv6 скрыт]');
    return s.split(K+'B'+K).join(build);
  }
  function openReport(){
    var pre=G('repPre');if(!pre)return;
    pre.textContent=sanitize(reportLines());
    var pv=G('repPrev');if(pv)pv.hidden=false;
    txt('repMsg','предпросмотр: проверьте текст и нажмите «Скопировать»');txt('repCopyMsg','');
  }
  function fallbackCopy(t){
    try{
      var ta=document.createElement('textarea');ta.value=t;ta.setAttribute('readonly','');
      ta.style.position='fixed';ta.style.top='-1000px';ta.style.left='-1000px';
      document.body.appendChild(ta);ta.focus();ta.select();
      var ok=false;try{ok=document.execCommand('copy')}catch(e){}
      document.body.removeChild(ta);return !!ok;
    }catch(e){return false}
  }
  function copyReport(){
    var pre=G('repPre');if(!pre)return;
    var t=pre.textContent||'';
    function done(ok){txt('repCopyMsg',ok?'скопировано в буфер':'не удалось — выделите текст вручную')}
    if(navigator.clipboard&&navigator.clipboard.writeText)
      navigator.clipboard.writeText(t).then(function(){done(true)}).catch(function(){done(fallbackCopy(t))});
    else done(fallbackCopy(t));
  }
  function init(){
    if(!G('evBox')&&!G('diagBox'))return;
    var eb=G('evBox'),db=G('diagBox');
    if(eb)eb.addEventListener('toggle',function(){if(eb.open)fetchAll()});
    if(db)db.addEventListener('toggle',function(){if(db.open)fetchAll()});
    var rb=G('evReload');if(rb)rb.addEventListener('click',fetchAll);
    var ab=G('repBtn');if(ab)ab.addEventListener('click',openReport);
    var cb=G('repCopy');if(cb)cb.addEventListener('click',copyReport);
    var xb=G('repClose');if(xb)xb.addEventListener('click',function(){
      var pv=G('repPrev');if(pv)pv.hidden=true;
      txt('repMsg','предпросмотр закрыт · без IP, MAC, SSID, токенов, device id и значений')});
    window.domP13={report:reportLines,sanitize:sanitize,fallback:fallbackCopy};
    var prev=window.domStatusTick||function(){};
    window.domStatusTick=function(d){try{prev(d)}catch(e){}try{tick(d)}catch(e){}};
    renderAll();
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();

)HJS";

static const char CONTROL_JS[] PROGMEM = R"CTL(
/* Shared transport: bounded requests, explicit incomplete-history handling and control access. */
(function(){
  'use strict';
  var original=window.fetch.bind(window), key='';
  var activeReads=0,readQueue=[];
  function queuedRead(action){
    return new Promise(function(resolve,reject){
      function start(){
        activeReads++;var released=false;
        function release(){if(released)return;released=true;activeReads--;if(readQueue.length)readQueue.shift()()}
        Promise.resolve().then(function(){return action(release)}).then(resolve,function(error){release();reject(error)});
      }
      if(activeReads<2)start();else readQueue.push(start);
    });
  }
  try {
    key=sessionStorage.getItem('dom.control')||'';
    var params=new URLSearchParams(location.hash.slice(1)), supplied=params.get('control');
    if(supplied&&/^[a-f0-9]{64}$/i.test(supplied)){
      key=supplied;sessionStorage.setItem('dom.control',key);
      history.replaceState(null,'',location.pathname+location.search);
    }
  }catch(e){}
  window.fetch=function(input,options){
    var url=new URL(typeof input==='string'?input:input.url,location.href);
    var local=url.origin===location.origin;
    var method=((options&&options.method)||(input&&input.method)||'GET').toUpperCase();
    var commands=['/tap','/reboot','/rotate','/restorebak','/beep','/update','/wifiset'];
    var readOnly=local&&method==='GET'&&((!url.search&&commands.indexOf(url.pathname)<0)||['/history','/range','/days','/files','/full'].indexOf(url.pathname)>=0);
    var readUntil=Date.now()+((options&&options.timeoutMs)||10000),releaseSlot=function(){};
    function send(retry){
      if(readOnly&&Date.now()>=readUntil)return Promise.reject(new DOMException('Read timeout','AbortError'));
      var init=Object.assign({},options||{}), headers=new Headers(init.headers||(input&&input.headers)||{});
      if(local&&key)headers.set('X-Dom-Key',key);
      var controller=new AbortController(), outer=init.signal;
      if(outer){if(outer.aborted)controller.abort();else outer.addEventListener('abort',function(){controller.abort()},{once:true})}
      var timeout=setTimeout(function(){controller.abort();releaseSlot()},readOnly?Math.max(1,readUntil-Date.now()):(init.timeoutMs||10000));
      delete init.timeoutMs;init.headers=headers;init.signal=controller.signal;if(local)init.redirect='error';
      return original(input,init).then(function(response){
        if(local&&response.status===401&&!retry){
          clearTimeout(timeout);
          var entered=window.prompt('Введите ключ управления устройством (доступен в приложении Windows).');
          if(!entered||!/^[a-f0-9]{64}$/i.test(entered.trim()))throw Error('Доступ к управлению не предоставлен');
          key=entered.trim();try{sessionStorage.setItem('dom.control',key)}catch(e){}
          return send(true);
        }
        // Ошибочный ответ (4xx/5xx) часто не читают, а слот чтения держится до
        // чтения тела или таймаута: два таких подряд залипали на 10 с. Слот
        // освобождаем сразу — release() идемпотентен, повторное чтение работает.
        if(local && !response.ok && releaseSlot) releaseSlot();
        ['json','text','blob','arrayBuffer'].forEach(function(name){
          var method=response[name].bind(response);
          response[name]=function(){return method().then(function(body){clearTimeout(timeout);releaseSlot();return body},function(error){clearTimeout(timeout);releaseSlot();throw error})};
        });
        return response;
      },function(error){clearTimeout(timeout);throw error});
    }
    if(!readOnly)return send(false);
    return queuedRead(function(release){
      releaseSlot=release;
      function attempt(count){return send(false).catch(function(error){
        if(options&&options.signal&&options.signal.aborted)throw error;
        if(error.name==='TypeError'&&count<2&&Date.now()+250<readUntil)return new Promise(function(resolve){setTimeout(resolve,250)}).then(function(){return attempt(count+1)});
        throw error;
      })}
      return attempt(0);
    });
  };
  window.domJson=function(url,deadlineMs,signal){
    var until=Date.now()+(deadlineMs||240000),failures=0;
    function read(){
      if(signal&&signal.aborted)return Promise.reject(Error('Загрузка отменена'));
      if(Date.now()>=until)return Promise.reject(Error('Время загрузки истории истекло'));
      return fetch(url,{cache:'no-store',signal:signal,timeoutMs:Math.min(10000,Math.max(1,until-Date.now()))}).then(function(r){
        if(!r.ok){var error=Error('HTTP '+r.status);error.retryable=[408,429,500,502,503,504].indexOf(r.status)>=0;throw error}
        return r.json().catch(function(error){error.retryable=true;throw error});
      }).catch(function(error){
        if(signal&&signal.aborted)throw error;
        if(Date.now()<until&&(error.retryable||error.name==='TypeError'||error.name==='AbortError')){
          return new Promise(function(resolve){setTimeout(resolve,Math.min(until-Date.now(),2000,400*++failures))}).then(read);
        }
        throw error;
      }).then(function(j){
        if(j.pending){if(Date.now()>=until)throw Error('Время загрузки истории истекло');return new Promise(function(resolve){setTimeout(resolve,Math.max(200,Math.min(1500,+j.retryMs||700)))}).then(read)}
        if(j.trunc)throw Error('Плата вернула неполную историю. Повторите загрузку.');
        if(j.ok===false)throw Error(j.err||'Ошибка устройства');
        return j;
      });
    }
    return read();
  };
})();

)CTL";
