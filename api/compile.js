// POST /compile { source, stdin } -> full compiler result JSON.
// Vercel serverless counterpart of lucid-server's /compile: runs the C++
// compiler (bin/lucid-json, built by vercel-build.sh) and returns its JSON.
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const BUNDLED = path.join(process.cwd(), "bin", "lucid-json");
const MAX_SOURCE_BYTES = 256 * 1024;

// Some bundles lose the executable bit; fall back to a copy in /tmp.
let binary = BUNDLED;
function ensureExecutable() {
  try { fs.accessSync(binary, fs.constants.X_OK); return; } catch (e) {}
  const copy = "/tmp/lucid-json";
  fs.copyFileSync(BUNDLED, copy);
  fs.chmodSync(copy, 0o755);
  binary = copy;
}

function internalError(message) {
  return {
    tokens: [], ast: null, symbolTable: null, optimized: null, output: [], success: false,
    errors: [{ phase: "server", type: "InternalError", message, line: 0, col: 0, snippet: "" }],
  };
}

module.exports = (req, res) => {
  if (req.method === "OPTIONS") return res.status(204).end();
  if (req.method !== "POST") return res.status(405).json({ error: { code: 405, message: "Use POST." } });

  const body = typeof req.body === "string" ? safeParse(req.body) : req.body;
  if (!body || typeof body.source !== "string")
    return res.status(400).json({ error: { code: 400, message: 'Body must be JSON: { "source": "..." }' } });
  if (Buffer.byteLength(body.source) > MAX_SOURCE_BYTES)
    return res.status(413).json({ error: { code: 413, message: "Program too large." } });

  try { ensureExecutable(); }
  catch (e) { return res.status(500).json(internalError("Compiler binary missing: " + e.message)); }

  // bin/ may also hold the C++ runtime the binary was linked against.
  const env = { ...process.env, LD_LIBRARY_PATH: path.dirname(BUNDLED) };
  const child = spawn(binary, [], { timeout: 20000, env });
  let out = "";
  child.stdout.on("data", (d) => { out += d; });
  child.on("error", (e) => res.status(500).json(internalError("Could not start the compiler: " + e.message)));
  child.on("close", (code, signal) => {
    if (res.headersSent) return;
    if (out) return res.setHeader("Content-Type", "application/json").status(200).send(out);
    res.status(200).json(internalError(signal ? "The compiler was stopped (time limit)." : `Compiler exited with code ${code}.`));
  });
  child.stdin.end(JSON.stringify({ source: body.source, stdin: typeof body.stdin === "string" ? body.stdin : "" }));
};

function safeParse(text) {
  try { return JSON.parse(text); } catch (e) { return null; }
}
