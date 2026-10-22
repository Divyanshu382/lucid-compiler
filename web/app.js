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
const LUCID_SPEC = `Lucid compiles a subset of C++. Write standard C++ using only these features:
- #include <iostream> / <string> and using namespace std; (accepted and ignored).
- Types: int, bool, string (std::string). const is allowed. No double, char, arrays, vectors,
  pointers, references, structs or classes.
- Variables: int x = 5;  int y;  int a = 1, b = 2;  Globals are allowed and visible in functions.
- Output: cout << "x = " << x << endl;  (endl or '\\n' end the line; a bool prints as 1 or 0)
- Input:  cin >> name >> n;
- Statements: if / else if / else, while, do-while, for, break, continue, return; braces
  are optional around single statements, as in C++.
- Operators: + - * / % (ints), << >> (shifts), < > <= >= == != (ints, or two strings),
  && || !, ?:, = += -= *= /= %=, ++ and -- as whole statements (x++; not y = x++).
  ints and bools convert to each other as in C++.
- Strings: s1 + s2 joins two strings; to join a number use to_string(n).
- Functions: int add(int a, int b) { return a + b; }  void functions allowed; recursion allowed.
  Put the program in int main() { ... return 0; }`;

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
// The STDIN box appears when the program reads with cin (or already has input).
function updateStdinVisibility() {
  $("stdinPane").hidden = !(/\bcin\s*>>/.test(editor.value) || $("stdinBox").value.trim());
}

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
    body: JSON.stringify({ source, stdin: $("stdinBox").value }),
  });
  if (!res.ok) throw new Error("server responded " + res.status);
  return res.json();
}

async function runCompile() {
  updateStdinVisibility();
  setStatus("busy", "compiling…");
  try {
    const result = await compileRemote(editor.value);
    lastResult = result;
    renderAll(result);
    const n = result.errors.length;
    setStatus(result.success ? "ok" : "err",
              result.success ? "compiled" : `${n} problem${n === 1 ? "" : "s"}`);
  } catch (err) {
    setStatus("err", "server offline");
    showServerOffline(err);
  }
}

function setStatus(cls, text) {
  statusPill.className = "status " + cls;
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
  safe(() => renderOutput(r.output || [], r), "output");
  safe(() => renderOptimizer(r.optimized), "optimizer");
  safe(() => renderIR(r.ir), "ir");
  safe(() => renderPerf(r.timings, r), "perf");
}

function renderPipeline(r) {
  const order = ["lexical", "syntax", "semantic", "optimizer", "ircode", "runtime"];
  // The earliest phase that reported an error; phases after it did not run.
  let failIdx = -1;
  for (const e of (r.errors || [])) {
    const idx = order.indexOf(e.phase);
    if (idx !== -1 && (failIdx === -1 || idx < failIdx)) failIdx = idx;
  }
  document.querySelectorAll(".rail .stage[data-phase]").forEach((el) => {
    const idx = order.indexOf(el.dataset.phase);
    el.classList.remove("ok", "err", "skip");
    el.classList.add(failIdx === -1 || idx < failIdx ? "ok" : idx === failIdx ? "err" : "skip");
  });

  const tokens = (r.tokens || []).filter((t) => t.type !== "EndOfFile").length;
  const count = (n) => (n ? 1 + (n.children || []).reduce((s, c) => s + count(c), 0) : 0);
  const st = r.symbolTable;
  const plural = (n, w) => `${n} ${w}${n === 1 ? "" : "s"}`;
  const metric = {
    "m-tokens":    r.tokens && r.tokens.length ? plural(tokens, "token") : "–",
    "m-ast":       r.ast ? plural(count(r.ast), "node") : "–",
    "m-symbols":   st ? plural((st.variables || []).length + (st.functions || []).length, "symbol") : "–",
    "m-optimizer": r.optimized ? (r.optimized.removed ? `−${r.optimized.removed} nodes` : "no change") : "–",
    "m-ir":        r.ir ? plural(r.ir.quads, "quad") : "–",
    "m-output":    r.optimized ? plural((r.output || []).length, "line") : "–",
    "m-perf":      r.timings ? fmtMicros(r.timings.total) : "–",
  };
  for (const [id, text] of Object.entries(metric)) $(id).textContent = text;

  const passed = failIdx === -1 ? 6 : failIdx;
  $("summary").textContent = `${passed}/6 phases passed` + (r.timings ? ` · ${fmtMicros(r.timings.total)}` : "");
}

function fmtMicros(us) {
  return us >= 1000 ? (us / 1000).toFixed(1) + " ms" : Math.round(us) + " µs";
}

// Token families share the colours of the syntax tree legend.
function tokenClass(type) {
  if (type.startsWith("Keyword")) return "tk-kw";
  if (type === "IntLiteral") return "tk-num";
  if (type === "StringLiteral") return "tk-str";
  if (type === "Identifier") return "tk-id";
  if (/^(Semicolon|Comma|Colon|LParen|RParen|LBrace|RBrace)$/.test(type)) return "tk-punct";
  return "tk-op";
}

function renderTokens(tokens) {
  const rows = tokens
    .filter((t) => t.type !== "EndOfFile")
    .map((t) => `<tr>
        <td class="pos">${t.line}:${t.col}</td>
        <td class="type ${tokenClass(t.type)}">${escapeHtml(t.type)}</td>
        <td class="val ${tokenClass(t.type)}">${escapeHtml(t.type === "StringLiteral" ? '"' + t.value + '"' : t.value)}</td>
      </tr>`)
    .join("");
  $("panel-tokens").innerHTML =
    `<div class="tok-count">${tokens.filter(t=>t.type!=="EndOfFile").length} tokens</div>
     <table class="tokens">
       <thead><tr><th>Line:Col</th><th>Token</th><th>Lexeme</th></tr></thead>
       <tbody>${rows}</tbody>
     </table>`;
}

// ---- graphical AST (SVG) ----
// Node families for the syntax tree; colours live in style.css so both themes work.
const KIND_FAMILY = {
  Program: "box", Block: "box",
  VarDecl: "stmt", Assign: "stmt", If: "stmt", While: "stmt", Print: "stmt",
  FuncDecl: "stmt", Return: "stmt", Break: "stmt", Continue: "stmt",
  Call: "call",
  BinaryOp: "expr", UnaryOp: "expr", Conditional: "expr",
  Identifier: "name",
  IntLiteral: "lit", StringLiteral: "lit", BoolLiteral: "lit",
};

function renderAst(ast) {
  const panel = $("panel-ast");
  if (!ast) {
    panel.innerHTML = `<p class="muted">No syntax tree: the lexer or parser stopped early. Fix the problems listed under the editor.</p>`;
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
    const fam = KIND_FAMILY[n.kind] || "box";
    svg += `<g class="node nk-${fam}"><title>${escapeHtml(n.kind)}, line ${n.line}</title>
      <rect x="${x - n.w / 2}" y="${y}" width="${n.w}" height="${boxH}" rx="3"/>
      <text class="label" x="${x}" y="${y + boxH / 2 + 4}" text-anchor="middle">${escapeHtml(trunc(n.label))}</text>
    </g>`;
  }
  svg += `</svg>`;
  panel.innerHTML = `<div class="legend">
      <span class="nk-stmt"><i></i>Statement</span><span class="nk-expr"><i></i>Operator</span>
      <span class="nk-call"><i></i>Call</span><span class="nk-name"><i></i>Name</span>
      <span class="nk-lit"><i></i>Literal</span>
    </div>
    <p class="ast-hint">Abstract syntax tree built by the parser. Hover a node for its kind and source line.</p>
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

function renderOutput(out, r) {
  const n = out.length;
  $("outputTitle").textContent = n ? `${n} line${n === 1 ? "" : "s"}` : "";
  const list = $("outputList");
  if (!n) {
    const ran = r && r.optimized;      // the interpreter only runs when phases 1-5 succeed
    list.innerHTML = ran ? `<div class="output-empty">The program printed nothing.</div>`
                         : `<div class="output-empty not-run">Not run: fix the problems below first.</div>`;
    return;
  }
  list.innerHTML = `<div class="output-box">${out.map((l) => `<div class="out-line">${escapeHtml(l) || " "}</div>`).join("")}</div>`;
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
    <button id="explainIrBtn" class="ghost ai-inline" type="button">Explain this code with AI</button>
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

const PHASE_TAG = { lexical: "lexer", syntax: "parser", semantic: "semantic", runtime: "runtime", server: "server" };

function renderErrors(errors) {
  const pane = $("errorsPane");
  const list = $("errorsList");
  $("errorsTitle").textContent = errors.length ? `Problems · ${errors.length}` : "Problems";
  if (!errors.length) {
    list.innerHTML = `<div class="no-errors">No problems. The program compiled and ran.</div>`;
    return;
  }
  list.innerHTML = errors.map((e, i) => {
    const runtime = e.phase === "runtime";
    return `<div class="error-item ${runtime ? "runtime" : ""}">
      <div class="error-top">
        <span class="error-phase">${escapeHtml(PHASE_TAG[e.phase] || e.phase)}</span>
        <strong>${escapeHtml(e.type)}</strong>
        <span class="error-loc">line ${e.line}${e.col ? ":" + e.col : ""}</span>
      </div>
      <div class="error-msg">${escapeHtml(e.message)}</div>
      ${e.snippet ? `<div class="error-snip">${escapeHtml(e.snippet)}</div>` : ""}
      <div class="error-actions">
        <button class="explain" data-idx="${i}" type="button">Explain</button>
        <button class="fix" data-idx="${i}" type="button">Fix with AI</button>
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
  if (name === "output") {            // the output lives under the editor, not in a panel
    const pane = $("outputPane");
    pane.scrollIntoView({ block: "nearest" });
    pane.classList.remove("flash"); void pane.offsetWidth; pane.classList.add("flash");
    return;
  }
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
`You are a friendly teaching assistant for Lucid, a compiler for a subset of C++.
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

  openModal("Explain " + errorObj.type,
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
`You are fixing a C++ program written for the Lucid compiler.

${LUCID_SPEC}

A C++ compiler reported this error (authoritative — fix exactly this, change as
little as possible, keep the program's intent):
  ${errorObj.phase} / ${errorObj.type} at line ${errorObj.line}: ${errorObj.message}

Here is the full program:
----
${source}
----
Return ONLY the corrected, complete program. No explanation, no markdown.`;
  const text = await geminiCall(prompt, apiKey);
  return stripFences(text);
}

// The agent: perceive (error) -> act (ask for fix) -> observe (recompile), x3.
async function runAutoFix(errorObj) {
  const apiKey = getKey();
  openModal("Fix with AI: propose, recompile, repeat", "");
  if (!aiReady(apiKey)) {
    const prompt = "Fix the program for: " + errorObj.type + " — " + errorObj.message;
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
        <div class="attempt-head">Attempt ${attempt}: the AI request failed</div>
        <p class="muted">${escapeHtml(String(e))}</p></div>`);
      return;
    }

    // Observe: re-validate by re-compiling on the C++ server.
    let result;
    try {
      result = await compileRemote(corrected);
    } catch (e) {
      setModalBody(doneHtml + `<div class="attempt fail">
        <div class="attempt-head">Attempt ${attempt}: recompiling failed</div>
        <p class="muted">${escapeHtml(String(e))}</p></div>`);
      return;
    }

    const ok = result.success;
    const next = result.errors[0] || {};
    doneHtml += `<div class="attempt ${ok ? "ok" : "fail"}">
      <div class="attempt-head">Attempt ${attempt}: ${ok ? "compiles cleanly" : "still has errors"}</div>
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
      setStatus("ok", "fixed by AI");
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
`Explain, in plain English for a beginner, what this C++ program does step by
step. Keep it concise (a short paragraph or a few
bullets). Do not rewrite the code.

Program:
----
${editor.value}
----`;
  openModal("What this program does", `<p><span class="spinner"></span> Asking Gemini…</p>`);
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
in C++ (compiled by Lucid, a C++ subset compiler) and the three-address code (quadruples) that the
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
  openModal("Explain the intermediate code", `<p><span class="spinner"></span> Asking Gemini…</p>`);
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
  "Read a number n and print its multiplication table",
  "Check whether 97 is a prime number",
  "Print the multiplication table of 7",
  "Add up the even numbers from 1 to 50",
];

function openWriteCode() {
  openModal("Write a program from a description", `
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
`Write a complete C++ program for the Lucid compiler.

${LUCID_SPEC}

Task: ${desc}

Use cout to show the results. Keep it short and readable, with a comment or two.
Return ONLY the C++ program. No explanation, no markdown.`;
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
      ? `<p class="verdict ok">The compiler accepted it. Output: <code>${escapeHtml(shown.join("  ·  "))}${result.output.length > 8 ? "  …" : ""}</code></p>`
      : `<p class="verdict bad">The compiler rejected it — <strong>${escapeHtml(result.errors[0].type)}</strong>
           (line ${result.errors[0].line}): ${escapeHtml(result.errors[0].message)}</p>`;
    out.innerHTML = `<pre>${escapeHtml(code)}</pre>${verdict}
      <button id="writeUse" class="primary">${result.success ? "Load into editor" : "Load it and fix with AI"}</button>
      ${footer}`;
    $("writeUse").addEventListener("click", () => {
      editor.value = code;
      updateGutter();
      lastResult = result;
      renderAll(result);
      if (result.success) { $("aiModal").hidden = true; setStatus("ok", "written by AI"); }
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
  const groups = {};
  window.LUCID_EXAMPLES.forEach((ex, i) => {
    const name = ex.group || "Programs";
    if (!groups[name]) { groups[name] = document.createElement("optgroup"); groups[name].label = name; sel.appendChild(groups[name]); }
    const opt = document.createElement("option");
    opt.value = i; opt.textContent = ex.name;
    groups[name].appendChild(opt);
  });
  sel.addEventListener("change", () => {
    editor.value = window.LUCID_EXAMPLES[+sel.value].code;
    $("stdinBox").value = window.LUCID_EXAMPLES[+sel.value].stdin || "";
    updateGutter();
    runCompile();
  });
}

// Light / dark. The first click flips whatever is showing now (which may come
// from the OS setting); the choice is remembered.
function currentTheme() {
  const set = document.documentElement.dataset.theme;
  if (set) return set;
  return matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}
function syncThemeButton() {
  $("themeBtn").textContent = currentTheme() === "dark" ? "Light" : "Dark";
}
$("themeBtn").addEventListener("click", () => {
  const next = currentTheme() === "dark" ? "light" : "dark";
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem("lucid_theme", next); } catch (e) {}
  syncThemeButton();
});
syncThemeButton();

// ---- resizable sections ----------------------------------------------------
// A handle sits above the output and above the problems list. Dragging it
// sets the height of the section below it; the code editor takes whatever is
// left. Sizes are remembered; double-click a handle to reset its section.
const MIN_SECTION = 44, MIN_EDITOR = 120;
function sizeKey(id) { return "lucid_h_" + id; }
function setSectionHeight(section, px) {
  const col = section.parentElement;
  const editorH = document.querySelector(".editor-wrap").getBoundingClientRect().height;
  const current = section.getBoundingClientRect().height;
  const max = current + editorH - MIN_EDITOR;          // never squeeze the editor below its minimum
  const h = Math.round(Math.max(MIN_SECTION, Math.min(px, max)));
  section.style.height = h + "px";
  section.style.maxHeight = "none";
  try { localStorage.setItem(sizeKey(section.id), h); } catch (e) {}
}
document.querySelectorAll(".resizer").forEach((handle) => {
  const section = $(handle.dataset.target);
  try { const saved = +localStorage.getItem(sizeKey(section.id)); if (saved) { section.style.height = saved + "px"; section.style.maxHeight = "none"; } } catch (e) {}

  handle.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    handle.setPointerCapture(e.pointerId);
    const startY = e.clientY, startH = section.getBoundingClientRect().height;
    handle.classList.add("dragging"); document.body.classList.add("resizing");
    const move = (ev) => setSectionHeight(section, startH - (ev.clientY - startY));   // drag up = taller
    const up = () => {
      handle.removeEventListener("pointermove", move);
      handle.classList.remove("dragging"); document.body.classList.remove("resizing");
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", up, { once: true });
    handle.addEventListener("pointercancel", up, { once: true });
  });
  handle.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowUp" && e.key !== "ArrowDown") return;
    e.preventDefault();
    const step = e.shiftKey ? 60 : 20;
    setSectionHeight(section, section.getBoundingClientRect().height + (e.key === "ArrowUp" ? step : -step));
  });
  handle.addEventListener("dblclick", () => {
    section.style.height = ""; section.style.maxHeight = "";
    try { localStorage.removeItem(sizeKey(section.id)); } catch (e) {}
  });
});

$("runBtn").addEventListener("click", runCompile);
$("stdinBox").addEventListener("input", scheduleCompile);
$("explainProgramBtn").addEventListener("click", explainProgram);
$("writeCodeBtn").addEventListener("click", openWriteCode);

(function init() {
  loadExamples();
  const savedKey = localStorage.getItem("lucid_gemini_key");
  if (savedKey) $("apiKey").value = savedKey;
  fetch("/ai/status").then((r) => r.json()).then((st) => {
    serverAi = !!st.serverKey;
    if (serverAi) $("apiKey").placeholder = "Using the server\u2019s key";
  }).catch(() => { /* older server without /ai: visitors paste their own key */ });
  const params = new URLSearchParams(location.search);
  const exParam = parseInt(params.get("ex"), 10);
  const exIndex = Number.isInteger(exParam) && window.LUCID_EXAMPLES[exParam] ? exParam : 0;
  $("examples").value = exIndex;
  // ?code=<base64> lets you share a program as a link; otherwise load an example.
  let initialCode = window.LUCID_EXAMPLES[exIndex].code;
  $("stdinBox").value = window.LUCID_EXAMPLES[exIndex].stdin || "";
  const codeParam = params.get("code");
  if (codeParam) { try { initialCode = decodeURIComponent(escape(atob(codeParam))); } catch (e) {} }
  editor.value = initialCode;
  updateGutter();
  const hash = location.hash.replace("#", "");
  if (hash) activateTab(hash);
  runCompile();
})();
