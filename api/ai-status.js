// GET /ai/status -> whether the deployment holds AI keys (never the keys).
// Keys are Vercel environment variables: GEMINI_API_KEY, DEEPSEEK_API_KEY.
module.exports = (req, res) => {
  res.status(200).json({
    serverKey: !!(process.env.GEMINI_API_KEY || "").trim(),
    backupKey: !!(process.env.DEEPSEEK_API_KEY || "").trim(),
  });
};
