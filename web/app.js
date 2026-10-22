// ============================================================================
// app.js — Lucid playground front-end.
// Responsibilities:
//   • send source to the C++ server (POST /compile) with a ~300ms debounce
//   • render tokens, a graphical AST, the symbol table, output, optimizer
//   • wire the Gemini assistant: explain error, auto-fix loop, explain program,
//     explain intermediate code, and write a program from a description.
//     All AI is optional: with no key the pipeline still works and we show the
//     exact request that *would* be sent.
//
// The authority rule: the AI is never part of the compiler. It never parses,
// type-checks, or decides whether a program is correct. It may explain, and it
// may PROPOSE source code — but every proposal is recompiled by the C++
// compiler on the server, and only the compiler's verdict counts.
// ============================================================================

// ---- Gemini config ----------------------------------------------------------
// Tried in order. A model that is busy (429/500/503), unknown (404) or too slow
// falls through to the next, so one overloaded model never breaks a live demo.
// 3.5 Flash leads because it answered fastest and most reliably in testing.
const GEMINI_MODELS = ["gemini-3.5-flash", "gemini-3.6-flash", "gemini-3.8-flash"];
const GEMINI_TIMEOUT_MS = 30000;
const geminiUrl = (model) =>
  `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent`;
let lastAiCall = null;   // { model, ms, via } of the most recent successful call

// True when the C++ server holds a Gemini key (GET /ai/status). Then AI works
// for every visitor with no key typed in: requests go to the server's /ai
// relay, which adds the key, so the key never reaches the browser.
let serverAi = false;
const aiReady = (apiKey) => !!apiKey || serverAi;

// One description of the language, shared by every prompt that writes code.
const LUCID_SPEC = `Lucid is a small statically-typed, C-like language.
- Types: int, string, bool. Declarations: int x = 5;  int y;  (y starts at 0, a
  string at "", a bool at false)  int a = 1, b = 2;
- Statements end with ';'. Blocks use { }. Comments: // line   and   /* block */
- Assignment: x = e;  x += e;  x -= e;  x *= e;  x /= e;  x %= e;  x++;  x--;  ++x;  --x;
- if (c) { } else if (c) { } else { }      (braces are required)
- while (c) { }    do { } while (c);    for (int i = 0; i < n; i++) { }
- break; and continue; inside loops.   Conditional expression: c ? a : b
- print(a, b, ...) prints the values on one line separated by spaces.
- "text" + n joins a number or bool onto a string.
- Functions, either style:
    func add(int a, int b): int { return a + b; }
    int add(int a, int b) { return a + b; }
  void functions return nothing (use plain return;). Recursion is allowed.
  If a program defines int main() and has no other top-level code, main() runs automatically.
- IMPORTANT: a function body can only see its own parameters and locals, plus
  other functions. It can NOT see global variables - pass values in as parameters.
- Operators: + (int addition or string joining), - * / % on ints,
  < > <= >= on ints, == != on two values of the same type, && || ! on bools.
- Conditions must be bool; there is no truthiness (if (n) is an error).
- No arrays, no char or float types, no input, no printf (use print).`;

// ---- DOM handles -----------------------------------------------------------
const $ = (id) => document.getElementById(id);
const editor = $("editor");
const gutter = $("gutter");
const statusPill = $("statusPill");

let lastResult = null;   // most recent compile result JSON

// ===========================================================================
// Editor plumbing
// ===========================================================================
function updateGutter() {
  const lines = editor.value.split("\n").length;
  let s = "";
  for (let i = 1; i <= lines; i++) s += i + "\n";
  gutter.textContent = s;
}
editor.addEventListener("scroll", () => { gutter.scrollTop = editor.scrollTop; });

// Insert real tabs (4 spaces) instead of losing focus.
editor.addEventListener("keydown", (e) => {
  if (e.key === "Tab") {
    e.preventDefault();
    const s = editor.selectionStart, en = editor.selectionEnd;
    editor.value = editor.value.slice(0, s) + "    " + editor.value.slice(en);
    editor.selectionStart = editor.selectionEnd = s + 4;
    scheduleCompile();
    updateGutter();
  }
});

let debounceTimer = null;
function scheduleCompile() {
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(runCompile, 300);
}
editor.addEventListener("input", () => { updateGutter(); scheduleCompile(); });

// ===========================================================================
// Compile (talk to the C++ server)
// ===========================================================================
async function compileRemote(source) {
  const res = await fetch("/compile", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ source }),
  });
  if (!res.ok) throw new Error("server responded " + res.status);
  return res.json();
}

async function runCompile() {
  setStatus("busy", "compiling…");
  try {
    const result = await compileRemote(editor.value);
    lastResult = result;
    renderAll(result);
    setStatus(result.success ? "ok" : "err",
              result.success ? "compiled ✓" : (result.errors.length + " error(s)"));
  } catch (err) {
    setStatus("err", "server offline");
    showServerOffline(err);
  }
}

function setStatus(cls, text) {
  statusPill.className = "pill " + cls;
  statusPill.textContent = text;
}

function showServerOffline(err) {
  const msg = `<p class="muted">Could not reach the C++ server at <code>/compile</code>.</p>
    <p class="muted">Start it with <code>./build/lucid-server</code> and open
    <code>http://localhost:8080</code> (don't open index.html directly as a file).</p>
    <p class="muted">Details: ${escapeHtml(String(err))}</p>`;
  $("panel-tokens").innerHTML = msg;
}

// ===========================================================================
// Rendering
// ===========================================================================
function renderAll(r) {
  // Each panel renders independently: a failure in one (say the AST layout for
  // some odd tree) must never stop the errors panel from updating — otherwise a
  // stale error would linger after you've already fixed the code. Errors and
  // the pipeline update first for exactly that reason.
  const safe = (fn, name) => { try { fn(); } catch (e) { console.error("render " + name + " failed:", e); } };
  safe(() => renderPipeline(r), "pipeline");
  safe(() => renderErrors(r.errors || []), "errors");
  safe(() => renderTokens(r.tokens || []), "tokens");
  safe(() => renderAst(r.ast), "ast");
  safe(() => renderSymbols(r.symbolTable), "symbols");
  safe(() => renderOutput(r.output || []), "output");
  safe(() => renderOptimizer(r.optimized), "optimizer");
  safe(() => renderIR(r.ir), "ir");
  safe(() => renderPerf(r.timings, r), "perf");
}

function renderPipeline(r) {
  const order = ["lexical", "syntax", "semantic", "optimizer", "ircode", "runtime"];
  // find earliest failing phase
  let failIdx = -1;
  for (const e of (r.errors || [])) {
    const idx = order.indexOf(e.phase);
    if (idx !== -1 && (failIdx === -1 || idx < failIdx)) failIdx = idx;
  }
  document.querySelectorAll(".pipeline .phase").forEach((el) => {
    const idx = order.indexOf(el.dataset.phase);
    el.classList.remove("ok", "err", "skip");
    if (failIdx === -1) el.classList.add("ok");
    else if (idx < failIdx) el.classList.add("ok");
    else if (idx === failIdx) el.classList.add("err");
    else el.classList.add("skip");
  });
}

function renderTokens(tokens) {
  const rows = tokens
    .filter((t) => t.type !== "EndOfFile")
    .map((t) => `<tr>
        <td class="pos">${t.line}:${t.col}</td>
        <td class="type">${escapeHtml(t.type)}</td>
        <td class="val">${escapeHtml(t.value)}</td>
      </tr>`)
    .join("");
  $("panel-tokens").innerHTML =
    `<div class="tok-count">${tokens.filter(t=>t.type!=="EndOfFile").length} tokens</div>
     <table class="tokens">
       <thead><tr><th>Pos</th><th>Type</th><th>Value</th></tr></thead>
       <tbody>${rows}</tbody>
     </table>`;
}

// ---- graphical AST (SVG) ----
const KIND_COLORS = {
  Program:   ["#30363d", "#8b98a9"],
  Block:     ["#30363d", "#8b98a9"],
  VarDecl:   ["#3a2c17", "#ffa657"],
  Assign:    ["#3a2c17", "#ffa657"],
  If:        ["#3a2c17", "#ffa657"],
  While:     ["#3a2c17", "#ffa657"],
  Print:     ["#3a2c17", "#ffa657"],
  FuncDecl:  ["#3a2c17", "#ffa657"],
  Return:    ["#3a2c17", "#ffa657"],
  Break:     ["#3a2c17", "#ffa657"],
  Continue:  ["#3a2c17", "#ffa657"],
  Conditional: ["#2a1f3d", "#d2a8ff"],
  Call:      ["#132a3f", "#6ea8fe"],
  BinaryOp:  ["#2a1f3d", "#d2a8ff"],
  UnaryOp:   ["#2a1f3d", "#d2a8ff"],
  Identifier:["#0f2f24", "#7ee787"],
  IntLiteral:   ["#12331f", "#56d364"],
  StringLiteral:["#12331f", "#56d364"],
  BoolLiteral:  ["#12331f", "#56d364"],
};

function renderAst(ast) {
  const panel = $("panel-ast");
  if (!ast) {
    panel.innerHTML = `<p class="muted">No AST — the lexer or parser stopped early. Fix the errors below.</p>`;
    return;
  }
  // Geometry.
  const vGap = 74, boxH = 30, marginX = 24, marginY = 24, sibGap = 16;
  const trunc = (s) => (s.length > 22 ? s.slice(0, 21) + "…" : s);
  const boxW = (label) => Math.min(190, Math.max(42, trunc(label).length * 6.8 + 18));

  // Layout: a single left-to-right pass. Each node is centered over its
  // children, but never allowed to overlap the previous node on the same row
  // (per-depth cursor accounts for real box widths — no overlap at any depth).
  let maxDepth = 0;
  const nodes = [], edges = [];
  const prevOnRow = {};   // depth -> {right edge x}
  (function walk(n, depth) {
    maxDepth = Math.max(maxDepth, depth);
    const w = boxW(n.label || "?");
    const kids = n.children || [];
    const node = { label: n.label || "?", kind: n.kind, line: n.line, depth, w };
    let x;
    if (kids.length === 0) {
      x = (prevOnRow[depth] ?? 0) + w / 2;
    } else {
      const cs = kids.map((k) => walk(k, depth + 1));
      const center = (cs[0].x + cs[cs.length - 1].x) / 2;
      const minX = (prevOnRow[depth] ?? 0) + w / 2;
      x = Math.max(center, minX);
      cs.forEach((c) => edges.push([node, c]));
    }
    node.x = x;
    prevOnRow[depth] = x + w / 2 + sibGap;
    nodes.push(node);
    return node;
  })(ast, 0);

  const px = (n) => ({ x: marginX + n.x, y: marginY + n.depth * vGap });
  let maxRight = 0;
  nodes.forEach((n) => { maxRight = Math.max(maxRight, n.x + n.w / 2); });
  const width = marginX * 2 + maxRight;
  const height = marginY * 2 + maxDepth * vGap + boxH;

  // Build SVG.
  let svg = `<svg class="ast" width="${Math.max(width, 200)}" height="${height}"
                  viewBox="0 0 ${Math.max(width, 200)} ${height}">`;
  for (const [p, c] of edges) {
    const a = px(p), b = px(c);
    const y1 = a.y + boxH, y2 = b.y;
    const my = (y1 + y2) / 2;
    svg += `<path class="edge" d="M ${a.x} ${y1} C ${a.x} ${my}, ${b.x} ${my}, ${b.x} ${y2}"/>`;
  }
  for (const n of nodes) {
    const { x, y } = px(n);
    const [fill, stroke] = KIND_COLORS[n.kind] || ["#30363d", "#8b98a9"];
    svg += `<g class="node"><title>${escapeHtml(n.kind)} — line ${n.line}</title>
      <rect x="${x - n.w / 2}" y="${y}" width="${n.w}" height="${boxH}" rx="7"
            fill="${fill}" stroke="${stroke}"/>
      <text class="label" x="${x}" y="${y + boxH / 2 + 4}" text-anchor="middle"
            fill="${stroke}">${escapeHtml(trunc(n.label))}</text>
    </g>`;
  }
  svg += `</svg>`;
  panel.innerHTML = `<div class="ast-hint">Graphical AST — hover a node for its kind &amp; source line. Scroll to pan.</div>
                     <div id="astSvgWrap">${svg}</div>`;
}

function renderSymbols(st) {
  const panel = $("panel-symbols");
  if (!st) { panel.innerHTML = `<p class="muted">No symbol table — semantic analysis did not run.</p>`; return; }
  const vars = st.variables || [];
  const funcs = st.functions || [];
  const varRows = vars.map((v) => `<tr>
      <td class="name">${escapeHtml(v.name)}</td>
      <td class="ty">${escapeHtml(v.type)}</td>
      <td><span class="badge">${escapeHtml(v.kind || "variable")}</span></td>
      <td class="muted">${escapeHtml(v.scope || "")}</td>
      <td class="muted">${v.line || ""}</td>
    </tr>`).join("");
  const funcRows = funcs.map((f) => `<tr>
      <td class="name">${escapeHtml(f.name)}</td>
      <td class="ty">${escapeHtml(f.returnType)}</td>
      <td class="muted">${escapeHtml(f.signature)}</td>
      <td class="muted">${f.line || ""}</td>
    </tr>`).join("");
  panel.innerHTML = `
    <div class="sym-group">
      <h3>Variables &amp; Parameters (${vars.length})</h3>
      ${vars.length ? `<table class="sym">
        <thead><tr><th>Name</th><th>Type</th><th>Kind</th><th>Scope</th><th>Line</th></tr></thead>
        <tbody>${varRows}</tbody></table>` : `<p class="muted">none</p>`}
    </div>
    <div class="sym-group">
      <h3>Functions (${funcs.length})</h3>
      ${funcs.length ? `<table class="sym">
        <thead><tr><th>Name</th><th>Returns</th><th>Signature</th><th>Line</th></tr></thead>
        <tbody>${funcRows}</tbody></table>` : `<p class="muted">none</p>`}
    </div>`;
}

function renderOutput(out) {
  const panel = $("panel-output");
  if (!out.length) { panel.innerHTML = `<div class="output-box output-empty">(no output)</div>`; return; }
  panel.innerHTML = `<div class="output-box">${out.map(escapeHtml).join("\n")}</div>`;
}

function renderOptimizer(opt) {
  const panel = $("panel-optimizer");
  if (!opt) { panel.innerHTML = `<p class="muted">Optimizer did not run (fix errors first).</p>`; return; }
  const before = opt.nodesBefore, after = opt.nodesAfter, removed = opt.removed;
  const pct = before > 0 ? Math.round((removed / before) * 100) : 0;
  panel.innerHTML = `
    <div class="opt-metrics">
      <div class="metric"><div class="num">${before}</div><div class="lbl">nodes before</div></div>
      <div class="metric"><div class="num">${after}</div><div class="lbl">nodes after</div></div>
      <div class="metric good"><div class="num">−${removed}</div><div class="lbl">removed (${pct}%)</div></div>
    </div>
    <div class="opt-bar-wrap">
      <div class="opt-bar"><span style="width:${100 - pct}%"></span></div>
      <div class="opt-sub">Constant folding + dead-code elimination shrank the tree by ${pct}%.</div>
    </div>
    <div class="opt-cols">
      <div><h4>Before</h4><div class="opt-tree">${escapeHtml(astText(opt.astBefore))}</div></div>
      <div><h4>After</h4><div class="opt-tree">${escapeHtml(astText(opt.astAfter))}</div></div>
    </div>`;
}

// ---- Intermediate code (three-address quadruples) -------------------------
function renderIR(ir) {
  const panel = $("panel-ir");
  if (!ir) { panel.innerHTML = `<p class="muted">Intermediate code is generated after semantic analysis succeeds.</p>`; return; }
  const kindOf = (op) =>
    op === "label" || op === "func" || op === "endfunc" ? "ir-label" :
    op === "goto" || op === "iffalse" ? "ir-jump" :
    op === "param" || op === "call" || op === "recv" || op === "ret" ? "ir-call" :
    op === "print" ? "ir-io" : "";
  const rows = ir.code.map((q) => {
    const flush = q.op === "label" || q.op === "func" || q.op === "endfunc";
    return `<tr class="${kindOf(q.op)}">
      <td class="ir-n">${q.n}</td>
      <td class="ir-text">${flush ? "" : "&nbsp;&nbsp;&nbsp;&nbsp;"}${escapeHtml(q.text)}</td>
      <td>${escapeHtml(q.op)}</td><td>${escapeHtml(q.arg1)}</td><td>${escapeHtml(q.arg2)}</td><td>${escapeHtml(q.result)}</td>
    </tr>`;
  }).join("");
  panel.innerHTML = `
    <div class="opt-metrics">
      <div class="metric"><div class="num">${ir.quads}</div><div class="lbl">quadruples</div></div>
      <div class="metric"><div class="num">${ir.temps}</div><div class="lbl">temporaries</div></div>
      <div class="metric"><div class="num">${ir.labels}</div><div class="lbl">labels</div></div>
    </div>
    <button id="explainIrBtn" class="ghost ai-inline">✨ Explain this intermediate code with AI</button>
    <p class="muted ir-note">Three-address code generated from the optimised tree. Each line has at most one operator;
      <span class="ir-key ir-jump">jumps</span> and <span class="ir-key ir-label">labels</span> implement if, while and for.</p>
    <table class="tokens ir-table">
      <thead><tr><th>#</th><th>Three-address code</th><th>op</th><th>arg1</th><th>arg2</th><th>result</th></tr></thead>
      <tbody>${rows || `<tr><td colspan="6" class="muted">(empty program)</td></tr>`}</tbody>
    </table>`;
  $("explainIrBtn").addEventListener("click", explainIR);
}

// ---- Per-phase timing --------------------------------------------------------
function renderPerf(t, r) {
  const panel = $("panel-perf");
  if (!t) { panel.innerHTML = `<p class="muted">No timing data.</p>`; return; }
  const phases = [
    ["lexer", "Lexer"], ["parser", "Parser"], ["semantic", "Semantic"],
    ["optimizer", "Optimizer"], ["ircode", "IR Code"], ["interpreter", "Interpreter"],
  ].filter(([k]) => k in t);
  const max = Math.max(1, ...phases.map(([k]) => t[k]));
  const fmt = (us) => us >= 1000 ? (us / 1000).toFixed(2) + " ms" : us.toFixed(1) + " µs";
  const bars = phases.map(([k, label]) => `
    <div class="perf-row">
      <span class="perf-label">${label}</span>
      <span class="perf-track"><span class="perf-fill" style="width:${Math.max(1.5, (t[k] / max) * 100)}%"></span></span>
      <span class="perf-val">${fmt(t[k])}</span>
    </div>`).join("");
  const opt = r && r.optimized;
  panel.innerHTML = `
    <div class="opt-metrics">
      <div class="metric"><div class="num">${fmt(t.total)}</div><div class="lbl">end to end, incl. JSON</div></div>
      <div class="metric"><div class="num">${(r.tokens || []).length}</div><div class="lbl">tokens</div></div>
      ${opt ? `<div class="metric${opt.removed ? " good" : ""}"><div class="num">${opt.removed ? "−" + opt.removed : "0"}</div><div class="lbl">nodes removed by optimiser</div></div>` : ""}
    </div>
    <h4 class="perf-h">Time per phase</h4>
    ${bars}
    <p class="muted ir-note">Measured with a monotonic clock inside the C++ compiler for this exact program.
      The end-to-end figure also covers serialising every phase to JSON for this page, so it exceeds the sum of the bars.
      Phases after a failing phase do not run, so they are not shown.</p>`;
}

// Compact indented text of an AST (for the optimizer before/after columns).
function astText(node, prefix = "", last = true, root = true) {
  if (!node) return "";
  let line = prefix + (root ? "" : (last ? "└─ " : "├─ ")) + node.label + "\n";
  const kids = node.children || [];
  const cp = prefix + (root ? "" : (last ? "   " : "│  "));
  kids.forEach((k, i) => { line += astText(k, cp, i === kids.length - 1, false); });
  return line;
}

function renderErrors(errors) {
  const pane = $("errorsPane");
  const list = $("errorsList");
  $("errorsTitle").textContent = errors.length ? `Errors (${errors.length})` : "Errors";
  if (!errors.length) {
    list.innerHTML = `<div class="no-errors">✓ No errors — the program compiled and ran.</div>`;
    return;
  }
  list.innerHTML = errors.map((e, i) => {
    const runtime = e.phase === "runtime";
    return `<div class="error-item ${runtime ? "runtime" : ""}">
      <div class="error-top">
        <span class="error-phase">${escapeHtml(e.phase)}</span>
        <strong>${escapeHtml(e.type)}</strong>
        <span class="error-loc">line ${e.line}${e.col ? ":" + e.col : ""}</span>
      </div>
      <div class="error-msg">${escapeHtml(e.message)}</div>
      ${e.snippet ? `<div class="error-snip">${escapeHtml(e.snippet)}</div>` : ""}
      <div class="error-actions">
        <button class="explain" data-idx="${i}">✨ Explain with AI</button>
        <button class="fix" data-idx="${i}">🔧 Fix it</button>
      </div>
    </div>`;
  }).join("");

  list.querySelectorAll(".explain").forEach((b) =>
    b.addEventListener("click", () => explainError(errors[+b.dataset.idx])));
  list.querySelectorAll(".fix").forEach((b) =>
    b.addEventListener("click", () => runAutoFix(errors[+b.dataset.idx])));
}

// ===========================================================================
// Tabs
// ===========================================================================
function activateTab(name) {
  const tab = document.querySelector(`.tab[data-tab="${name}"]`);
  if (!tab) return;
  document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
  document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
  tab.classList.add("active");
  $("panel-" + name).classList.add("active");
}
document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    activateTab(tab.dataset.tab);
    history.replaceState(null, "", "#" + tab.dataset.tab);
  });
});

// ===========================================================================
// Gemini Flash assistant
// ===========================================================================
function getKey() {
  const k = $("apiKey").value.trim();
  if (k) localStorage.setItem("lucid_gemini_key", k);
  return k;
}

function buildPayload(prompt) {
  return { contents: [{ parts: [{ text: prompt }] }] };
}

function offlinePayloadView(title, prompt) {
  const payload = JSON.stringify(buildPayload(prompt), null, 2);
  return `<h3>${title} — no API key set</h3>
    <p class="muted">Enter a Gemini API key above to enable AI. The pipeline still works without it.
    This is the <em>exact</em> request that would be sent:</p>
    <pre>POST ${geminiUrl(GEMINI_MODELS[0])}
header: x-goog-api-key: &lt;YOUR_KEY&gt;

${escapeHtml(payload)}</pre>`;
}

// How long a 429 reply asks us to wait, in ms (Gemini sends RetryInfo, e.g. "retryDelay": "7s").
function retryDelayMs(body) {
  const m = body.match(/"retryDelay"\s*:\s*"(\d+(?:\.\d+)?)s"/);
  return m ? Math.ceil(parseFloat(m[1]) * 1000) : null;
}

async function geminiCall(prompt, apiKey) {
  const viaServer = !apiKey;   // a pasted key takes priority over the server's
  const failures = [];
  const queue = [...GEMINI_MODELS];
  const retried = new Set();
  while (queue.length) {
    const model = queue.shift();
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), GEMINI_TIMEOUT_MS);
    const started = performance.now();
    let res;
    try {
      res = viaServer
        // Server relay: the server adds its own key; we send only model + prompt.
        ? await fetch("/ai", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ model, prompt }),
            signal: controller.signal,
          })
        // Visitor's own key: call Gemini directly from the browser.
        : await fetch(geminiUrl(model), {
            method: "POST",
            headers: { "Content-Type": "application/json", "x-goog-api-key": apiKey },
            body: JSON.stringify(buildPayload(prompt)),
            signal: controller.signal,
          });
    } catch (e) {
      failures.push(`${model}: ${e.name === "AbortError" ? "timed out" : "network error"}`);
      continue;
    } finally {
      clearTimeout(timer);
    }

    if (!res.ok) {
      const body = await res.text();
      // A bad or unauthorised key fails identically on every model: stop now.
      if (res.status === 400 || res.status === 401 || res.status === 403) {
        const whose = viaServer ? "the server's API key (gemini.key / GEMINI_API_KEY)" : "the API key you pasted";
        throw new Error(`Gemini rejected the request (HTTP ${res.status}) - check ${whose}. ${body.slice(0, 200)}`);
      }
      // Rate-limited (free tier): if Google says the wait is short, wait and
      // retry this model once before moving on.
      const wait = res.status === 429 ? retryDelayMs(body) : null;
      if (wait !== null && wait <= 15000 && !retried.has(model)) {
        retried.add(model);
        await new Promise((r) => setTimeout(r, wait + 250));
        queue.unshift(model);
        continue;
      }
      failures.push(`${model}: HTTP ${res.status}${res.status === 429 ? " (rate limit)" : ""}`);
      continue;   // busy or unavailable: try the next model
    }

    const data = await res.json();
    // Thinking models may split a reply across several parts; join the visible ones.
    const parts = data?.candidates?.[0]?.content?.parts || [];
    const text = parts.filter((p) => p.text && !p.thought).map((p) => p.text).join("");
    if (!text) { failures.push(`${model}: empty reply`); continue; }

    lastAiCall = { model, ms: Math.round(performance.now() - started), via: viaServer ? "server" : "browser" };
    return text;
  }
  throw new Error("Every model was unavailable. " + failures.join("; ") + ". "
    + "On the free tier this usually means too many requests in the last minute - wait a minute and try again.");
}

// "Answered by gemini-3.5-flash in 2.3 s" - shown under every AI reply.
function aiFooter() {
  if (!lastAiCall) return "";
  const how = lastAiCall.via === "server" ? " via the server's key" : " using your key";
  return `<p class="ai-footer">Answered by <code>${lastAiCall.model}</code> in ${(lastAiCall.ms / 1000).toFixed(1)} s${how}</p>`;
}

// Strip ```lang ... ``` fences the model may wrap code in.
function stripFences(s) {
  const m = s.match(/```[a-zA-Z]*\n?([\s\S]*?)```/);
  return (m ? m[1] : s).trim() + "\n";
}

function relevantLines(source, line, radius = 3) {
  const lines = source.split("\n");
  const start = Math.max(0, line - 1 - radius);
  const end = Math.min(lines.length, line - 1 + radius + 1);
  return lines.slice(start, end)
    .map((t, i) => `${start + i + 1}: ${t}`).join("\n");
}

// (1) explainError -> beginner explanation + concrete fix
async function explainError(errorObj) {
  const apiKey = getKey();
  const prompt =
`You are a friendly teaching assistant for a small language called Lucid.
A real C++ compiler has ALREADY diagnosed this error — trust it completely and
do NOT re-diagnose or invent a different error. In 3-4 short sentences, explain
in plain, beginner-friendly English what this error means and why it happened,
then give one concrete fix.

Compiler error (authoritative):
  phase:   ${errorObj.phase}
  type:    ${errorObj.type}
  line:    ${errorObj.line}
  message: ${errorObj.message}

Relevant source lines:
${relevantLines(editor.value, errorObj.line)}

Answer with two short sections titled "Explanation:" and "Fix:".`;

  openModal("✨ Explain: " + errorObj.type,
            `<p><span class="spinner"></span> Asking Gemini…</p>`);
  if (!aiReady(apiKey)) { setModalBody(offlinePayloadView("Explain error", prompt)); return; }
  try {
    const text = await geminiCall(prompt, apiKey);
    setModalBody(renderMarkdownish(text) + aiFooter());
  } catch (e) {
    setModalBody(`<h3>AI request failed</h3><p class="muted">${escapeHtml(String(e))}</p>`);
  }
}

// (2) autoFix -> corrected full source
async function autoFix(source, errorObj, apiKey) {
  const prompt =
`You are fixing a program written in "Lucid".

${LUCID_SPEC}

A C++ compiler reported this error (authoritative — fix exactly this, change as
little as possible, keep the program's intent):
  ${errorObj.phase} / ${errorObj.type} at line ${errorObj.line}: ${errorObj.message}

Here is the full program:
----
${source}
----
Return ONLY the corrected, complete Lucid program. No explanation, no markdown.`;
  const text = await geminiCall(prompt, apiKey);
  return stripFences(text);
}

// The agent: perceive (error) -> act (ask for fix) -> observe (recompile), x3.
async function runAutoFix(errorObj) {
  const apiKey = getKey();
  openModal("🔧 Auto-fix loop", "");
  if (!aiReady(apiKey)) {
    const prompt = "Fix the Lucid program for: " + errorObj.type + " — " + errorObj.message;
    setModalBody(offlinePayloadView("Auto-fix", prompt));
    return;
  }

  let source = editor.value;
  let currentError = errorObj;
  let doneHtml = "";   // finished attempts, kept as we go

  for (let attempt = 1; attempt <= 3; attempt++) {
    // Perceive/act: show a spinner while we ask Gemini for a fix.
    setModalBody(doneHtml + `<div class="attempt">
      <div class="attempt-head"><span class="spinner"></span> Attempt ${attempt}: asking Gemini to fix
      <em>${escapeHtml(currentError.type)}</em> …</div></div>`);

    let corrected;
    try {
      corrected = await autoFix(source, currentError, apiKey);
    } catch (e) {
      setModalBody(doneHtml + `<div class="attempt fail">
        <div class="attempt-head">⚠️ Attempt ${attempt}: AI request failed</div>
        <p class="muted">${escapeHtml(String(e))}</p></div>`);
      return;
    }

    // Observe: re-validate by re-compiling on the C++ server.
    let result;
    try {
      result = await compileRemote(corrected);
    } catch (e) {
      setModalBody(doneHtml + `<div class="attempt fail">
        <div class="attempt-head">⚠️ Attempt ${attempt}: re-compile failed</div>
        <p class="muted">${escapeHtml(String(e))}</p></div>`);
      return;
    }

    const ok = result.success;
    const next = result.errors[0] || {};
    doneHtml += `<div class="attempt ${ok ? "ok" : "fail"}">
      <div class="attempt-head">${ok ? "✅" : "⚠️"} Attempt ${attempt}: ${ok ? "compiles cleanly!" : "still has errors"}</div>
      <pre>${escapeHtml(corrected)}</pre>
      ${ok ? `<p class="muted">Applied to the editor.</p>`
           : `<p class="muted">Remaining: <strong>${escapeHtml(next.type || "error")}</strong> — ${escapeHtml(next.message || "")}</p>`}
    </div>`;
    setModalBody(doneHtml);

    if (ok) {
      editor.value = corrected;
      updateGutter();
      lastResult = result;
      renderAll(result);
      setStatus("ok", "fixed by AI ✓");
      return;
    }
    source = corrected;
    currentError = result.errors[0] || currentError;
  }
  setModalBody(doneHtml +
    `<p class="muted">Reached the 3-attempt limit. The remaining error may need a human — or a clearer program.</p>`);
}

// (3) explainProgram -> plain-English walkthrough
async function explainProgram() {
  const apiKey = getKey();
  const prompt =
`Explain, in plain English for a beginner, what this program written in the
"Lucid" language does step by step. Keep it concise (a short paragraph or a few
bullets). Do not rewrite the code.

Program:
----
${editor.value}
----`;
  openModal("📖 What this program does", `<p><span class="spinner"></span> Asking Gemini…</p>`);
  if (!aiReady(apiKey)) { setModalBody(offlinePayloadView("Explain program", prompt)); return; }
  try {
    const text = await geminiCall(prompt, apiKey);
    setModalBody(renderMarkdownish(text) + aiFooter());
  } catch (e) {
    setModalBody(`<h3>AI request failed</h3><p class="muted">${escapeHtml(String(e))}</p>`);
  }
}

// (4) explainIR -> how the source became three-address code
async function explainIR() {
  const ir = lastResult && lastResult.ir;
  if (!ir) return;
  const apiKey = getKey();
  const source = editor.value.split("\n").map((l, i) => `${i + 1}: ${l}`).join("\n");
  const listing = ir.code.map((q) => `${String(q.n).padStart(3)}  ${q.text}`).join("\n");
  const prompt =
`You are a teaching assistant for a compiler design course. Below is a program
in the small language "Lucid" and the three-address code (quadruples) that the
compiler generated from it. Explain to a student how the source was translated:
what the temporaries (t0, t1, ...) hold, and how the labels and jumps (iffalse,
goto) implement each if, while and for loop. Refer to instruction numbers and
source line numbers. Describe only the instructions listed - do not invent any.
Use short bullet points, about 150 words.

Source:
----
${source}
----

Three-address code:
----
${listing}
----`;
  openModal("✨ Explain the intermediate code", `<p><span class="spinner"></span> Asking Gemini…</p>`);
  if (!aiReady(apiKey)) { setModalBody(offlinePayloadView("Explain IR", prompt)); return; }
  try {
    const text = await geminiCall(prompt, apiKey);
    setModalBody(renderMarkdownish(text) + aiFooter());
  } catch (e) {
    setModalBody(`<h3>AI request failed</h3><p class="muted">${escapeHtml(String(e))}</p>`);
  }
}

// (5) writeProgram -> English description to Lucid, then verified by the compiler.
// The model proposes; the C++ compiler decides. A rejected program is handed
// straight to the auto-fix loop.
const WRITE_IDEAS = [
  "Print the first 10 Fibonacci numbers",
  "Check whether 97 is a prime number",
  "Print the multiplication table of 7",
  "Add up the even numbers from 1 to 50",
];

function openWriteCode() {
  openModal("✨ Write a program from a description", `
    <p class="muted">Describe a program in plain English. Gemini writes it in Lucid, then the
      C++ compiler checks it. The AI never decides whether its own code is correct.</p>
    <label class="field-label" for="writeDesc">What should the program do?</label>
    <textarea id="writeDesc" rows="3" placeholder="e.g. Print the first 10 Fibonacci numbers"></textarea>
    <div class="idea-row">${WRITE_IDEAS.map((i) =>
      `<button type="button" class="chip" data-idea="${escapeHtml(i)}">${escapeHtml(i)}</button>`).join("")}</div>
    <button id="writeGo" class="primary">Write it</button>
    <div id="writeResult"></div>`);
  document.querySelectorAll(".idea-row .chip").forEach((c) =>
    c.addEventListener("click", () => { $("writeDesc").value = c.dataset.idea; $("writeDesc").focus(); }));
  $("writeGo").addEventListener("click", writeProgram);
  $("writeDesc").addEventListener("keydown", (e) => {
    if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) writeProgram();
  });
  $("writeDesc").focus();
}

async function writeProgram() {
  const desc = $("writeDesc").value.trim();
  if (!desc) { $("writeDesc").focus(); return; }
  const apiKey = getKey();
  const prompt =
`Write a complete program in the language "Lucid".

${LUCID_SPEC}

Task: ${desc}

Use print to show the results. Keep it short and readable, with a comment or two.
Return ONLY the Lucid program. No explanation, no markdown.`;
  const out = $("writeResult");
  if (!aiReady(apiKey)) { out.innerHTML = offlinePayloadView("Write code", prompt); return; }

  const go = $("writeGo");
  go.disabled = true;
  out.innerHTML = `<p><span class="spinner"></span> Gemini is writing the program…</p>`;
  try {
    const code = stripFences(await geminiCall(prompt, apiKey));
    const footer = aiFooter();
    out.innerHTML = `<p><span class="spinner"></span> Checking it with the C++ compiler…</p>`;
    const result = await compileRemote(code);   // the compiler's verdict, not the model's
    const shown = (result.output || []).slice(0, 8);
    const verdict = result.success
      ? `<p class="verdict ok">✅ The compiler accepted it. Output: <code>${escapeHtml(shown.join("  ·  "))}${result.output.length > 8 ? "  …" : ""}</code></p>`
      : `<p class="verdict bad">⚠️ The compiler rejected it — <strong>${escapeHtml(result.errors[0].type)}</strong>
           (line ${result.errors[0].line}): ${escapeHtml(result.errors[0].message)}</p>`;
    out.innerHTML = `<pre>${escapeHtml(code)}</pre>${verdict}
      <button id="writeUse" class="primary">${result.success ? "Load into editor" : "Load it and fix with AI"}</button>
      ${footer}`;
    $("writeUse").addEventListener("click", () => {
      editor.value = code;
      updateGutter();
      lastResult = result;
      renderAll(result);
      if (result.success) { $("aiModal").hidden = true; setStatus("ok", "written by AI ✓"); }
      else runAutoFix(result.errors[0]);
    });
  } catch (e) {
    out.innerHTML = `<h3>AI request failed</h3><p class="muted">${escapeHtml(String(e))}</p>`;
  } finally {
    go.disabled = false;
  }
}

// ===========================================================================
// Modal
// ===========================================================================
function openModal(title, bodyHtml) {
  $("aiTitle").textContent = title;
  $("aiBody").innerHTML = bodyHtml;
  $("aiModal").hidden = false;
}
function setModalBody(html) { $("aiBody").innerHTML = html; }
$("aiClose").addEventListener("click", () => ($("aiModal").hidden = true));
$("aiModal").addEventListener("click", (e) => { if (e.target === $("aiModal")) $("aiModal").hidden = true; });

// ===========================================================================
// Helpers
// ===========================================================================
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// Very small markdown-ish renderer: **bold**, `code`, headings "X:", newlines.
function renderMarkdownish(text) {
  let html = escapeHtml(text)
    .replace(/\*\*(.+?)\*\*/g, "<strong>$1</strong>")
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/^(Explanation|Fix|Summary):/gim, "<h3>$1</h3>")
    .replace(/\n/g, "<br>");
  return html;
}

// ===========================================================================
// Boot
// ===========================================================================
function loadExamples() {
  const sel = $("examples");
  window.LUCID_EXAMPLES.forEach((ex, i) => {
    const opt = document.createElement("option");
    opt.value = i; opt.textContent = ex.name;
    sel.appendChild(opt);
  });
  sel.addEventListener("change", () => {
    editor.value = window.LUCID_EXAMPLES[+sel.value].code;
    updateGutter();
    runCompile();
  });
}

$("runBtn").addEventListener("click", runCompile);
$("explainProgramBtn").addEventListener("click", explainProgram);
$("writeCodeBtn").addEventListener("click", openWriteCode);

(function init() {
  loadExamples();
  const savedKey = localStorage.getItem("lucid_gemini_key");
  if (savedKey) $("apiKey").value = savedKey;
  fetch("/ai/status").then((r) => r.json()).then((st) => {
    serverAi = !!st.serverKey;
    if (serverAi) $("apiKey").placeholder = "AI is on for everyone (server key). Optional: use your own key";
  }).catch(() => { /* older server without /ai: visitors paste their own key */ });
  const params = new URLSearchParams(location.search);
  const exParam = parseInt(params.get("ex"), 10);
  const exIndex = Number.isInteger(exParam) && window.LUCID_EXAMPLES[exParam] ? exParam : 0;
  $("examples").value = exIndex;
  // ?code=<base64> lets you share a program as a link; otherwise load an example.
  let initialCode = window.LUCID_EXAMPLES[exIndex].code;
  const codeParam = params.get("code");
  if (codeParam) { try { initialCode = decodeURIComponent(escape(atob(codeParam))); } catch (e) {} }
  editor.value = initialCode;
  updateGutter();
  const hash = location.hash.replace("#", "");
  if (hash) activateTab(hash);
  runCompile();
})();
