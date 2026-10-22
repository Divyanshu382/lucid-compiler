// POST /ai { model, prompt } -> relayed Gemini (or DeepSeek backup) reply.
// Vercel serverless counterpart of lucid-server's /ai (src/ai_proxy.cpp): the
// keys stay in environment variables and never reach the browser. DeepSeek
// replies are converted into Gemini's reply shape, as on the C++ server.
const MAX_PROMPT_BYTES = 64 * 1024;
const TIMEOUT_MS = 28000;

function fail(res, status, message) {
  res.status(status).json({ error: { code: status, message } });
}

// Plain model ids only, e.g. "gemini-3.5-flash" or "deepseek-flash".
function isAllowedModel(model) {
  return typeof model === "string" && model.length >= 8 && model.length <= 64 &&
    /^(gemini|deepseek)-[a-z0-9.-]+$/.test(model);
}

async function postJson(url, headers, payload) {
  try {
    const r = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json", ...headers },
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(TIMEOUT_MS),
    });
    return { status: r.status, body: await r.text() };
  } catch (e) {
    const message = e.name === "TimeoutError"
      ? "The AI service did not answer within 28 seconds."
      : "The server could not reach the AI service.";
    return { status: 502, body: JSON.stringify({ error: { code: 502, message } }) };
  }
}

module.exports = async (req, res) => {
  if (req.method !== "POST") return fail(res, 405, "Use POST.");

  // Only the playground's own page may spend the keys.
  const origin = req.headers.origin;
  if (origin) {
    const hosts = [req.headers.host, req.headers["x-forwarded-host"]].filter(Boolean);
    let originHost = "";
    try { originHost = new URL(origin).host; } catch (e) {}
    if (!hosts.includes(originHost))
      return fail(res, 403, "AI requests are only accepted from the playground page itself.");
  }

  let body = req.body;
  if (typeof body === "string") { try { body = JSON.parse(body); } catch (e) { body = null; } }
  if (!body) return fail(res, 400, 'Request body must be JSON: { "model", "prompt" }.');
  const { model, prompt } = body;
  if (!isAllowedModel(model)) return fail(res, 400, "Unknown model id.");
  if (typeof prompt !== "string" || !prompt) return fail(res, 400, "Empty prompt.");
  if (Buffer.byteLength(prompt) > MAX_PROMPT_BYTES) return fail(res, 413, "Prompt too large.");

  const deepseek = model.startsWith("deepseek-");
  const key = ((deepseek ? process.env.DEEPSEEK_API_KEY : process.env.GEMINI_API_KEY) || "").trim();
  if (!key)
    return fail(res, 503, deepseek
      ? "This deployment has no DeepSeek backup key (DEEPSEEK_API_KEY)."
      : "This deployment has no Gemini key. Set GEMINI_API_KEY, or paste your own key in the playground.");

  let reply;
  if (deepseek) {
    reply = await postJson("https://api.deepseek.com/chat/completions",
      { Authorization: "Bearer " + key },
      { model, messages: [{ role: "user", content: prompt }] });
    if (reply.status === 200) {
      try {
        const text = JSON.parse(reply.body).choices[0].message.content;
        reply.body = JSON.stringify({ candidates: [{ content: { parts: [{ text }] } }] });
      } catch (e) {
        return fail(res, 502, "DeepSeek returned a reply in an unexpected format.");
      }
    }
  } else {
    reply = await postJson(
      `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent`,
      { "x-goog-api-key": key },
      { contents: [{ parts: [{ text: prompt }] }] });
  }
  res.setHeader("Content-Type", "application/json");
  res.status(reply.status).send(reply.body);
};
