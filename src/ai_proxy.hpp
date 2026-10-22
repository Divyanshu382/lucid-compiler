// ============================================================================
// ai_proxy.hpp — server-side relay to the Gemini API, used only by lucid-server.
//
// This is NOT part of the compiler. It lets the playground's AI features work
// for every visitor using one API key held by the server, so the key never
// reaches a browser and never appears in the repository.
//
// HTTPS is delegated to the system `curl` (shipped with macOS, Linux and
// Windows 10+), so the build still needs no TLS library. The key is handed to
// curl through its stdin config, never on the command line, so it cannot be
// seen in a process listing.
// ============================================================================
#pragma once

#include <string>

namespace lucid {

// Gemini's HTTP status and raw response body, passed through unchanged so the
// browser handles a relayed reply exactly like a direct one.
struct AiReply {
    int status;
    std::string body;
};

// A key held by the server: the environment variable `envName` if set,
// otherwise the first line of `keyFile`. Returns "" when neither exists.
// `source` describes where the key came from (never the key itself).
std::string loadServerKey(const std::string& envName, const std::string& keyFile, std::string& source);

// Accepts only plain model ids such as "gemini-3.5-flash" or "deepseek-flash".
// The id is spliced into the request, so anything else is refused.
bool isAllowedModel(const std::string& model);
inline bool isDeepSeekModel(const std::string& model) { return model.rfind("deepseek-", 0) == 0; }

// One generateContent request. Never throws; network failures come back as a
// 502 with a JSON error body in the same shape Gemini uses.
AiReply callGemini(const std::string& model, const std::string& prompt, const std::string& apiKey);

// The backup provider. DeepSeek's reply is converted into Gemini's reply
// shape, so the browser handles both providers in exactly the same way.
AiReply callDeepSeek(const std::string& model, const std::string& prompt, const std::string& apiKey);

} // namespace lucid
