// ============================================================================
// main_server.cpp — local playground server for Lucid (`lucid-server`).
// A thin wrapper over the compiler core using cpp-httplib. Serves the static
// web/ playground and exposes:
//   POST /compile    { source }        -> full compiler result JSON
//   GET  /ai/status                    -> whether the server holds a Gemini key
//   POST /ai         { model, prompt } -> relayed Gemini reply (ai_proxy.cpp)
// All compiler work stays in pure C++. With a server key, every visitor gets
// the AI features without ever seeing the key.
// ============================================================================
#include <chrono>
#include <filesystem>
#include <vector>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

// A browser keeps several persistent (keep-alive) connections open at once. With
// cpp-httplib's small default thread pool, those idle connections can occupy
// every worker and the page appears to "load forever" — even though curl (one
// short connection) works fine. Give the server plenty of worker threads so
// request handling can never be starved. Must be defined before httplib.h.
#define CPPHTTPLIB_THREAD_POOL_COUNT 64

#include "httplib.h"
#include "../third_party/json.hpp"
#include "compiler.hpp"
#include "ai_proxy.hpp"

#ifndef LUCID_WEB_DIR
#define LUCID_WEB_DIR "web"
#endif

namespace fs = std::filesystem;

// Locate the web/ folder at run time, so a binary built on one machine (or
// shipped prebuilt for Windows) works wherever the repository is cloned.
static std::string findWebDir(const char* argv0) {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("LUCID_WEB_DIR")) candidates.emplace_back(env);
    candidates.emplace_back(LUCID_WEB_DIR);                  // where it was built
    candidates.push_back(fs::current_path() / "web");        // run from the repo root
    std::error_code ec;
    fs::path exeDir = fs::absolute(argv0, ec).parent_path();
    for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path()) {
        candidates.push_back(dir / "web");                   // build/, windows/, ...
        if (dir == dir.root_path()) break;
    }
    for (const auto& c : candidates)
        if (fs::exists(c / "index.html", ec)) return fs::absolute(c, ec).string();
    return "web";
}

// Prompts are a program plus a few lines of instructions; anything far larger
// is not a playground request.
static const size_t kMaxPromptBytes = 64 * 1024;

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) {
        try { port = std::stoi(argv[1]); } catch (...) { port = 8080; }
    } else if (const char* env = std::getenv("PORT")) {
        try { port = std::stoi(env); } catch (...) {}
    }

#ifndef _WIN32
    // A client that disconnects mid-reply must not kill the server.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const std::string webDir = findWebDir(argv[0]);
    // gemini.key sits in the project root, next to web/.
    const std::string keyFile = (fs::path(webDir).parent_path() / "gemini.key").string();
    std::string keySource;
    const std::string serverKey = lucid::loadServerKey(keyFile, keySource);

    httplib::Server server;

    // Serve the playground (index.html, style.css, app.js, examples.js).
    if (!server.set_mount_point("/", webDir)) {
        std::cerr << "warning: could not mount web dir '" << webDir
                  << "' — static files will not be served.\n";
    }

    // Small CORS courtesy so the page also works if opened from a file:// origin.
    server.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                {"Access-Control-Allow-Headers", "Content-Type"},
                                {"Access-Control-Allow-Methods", "POST, OPTIONS"}});
    server.Options("/compile", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // POST /compile { "source": "..." } -> full compiler result JSON.
    server.Post("/compile", [](const httplib::Request& req, httplib::Response& res) {
        std::string source, input;
        try {
            auto body = nlohmann::json::parse(req.body);
            source = body.value("source", "");
            input = body.value("stdin", "");             // what cin reads
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content(R"({"error":{"code":400,"message":"Body must be JSON: { \"source\": \"...\" }"}})",
                            "application/json");
            return;
        }
        // Compiles and serialises on a large-stack thread (bigstack.hpp), so
        // deep recursion in the user's program cannot overflow this server
        // thread's small stack. A compiler failure becomes a clean error.
        try {
            res.set_content(lucid::compileToJsonText(source, input), "application/json");
        } catch (const std::exception& e) {
            nlohmann::json out = {
                {"tokens", nlohmann::json::array()}, {"ast", nullptr}, {"symbolTable", nullptr},
                {"optimized", nullptr}, {"output", nlohmann::json::array()}, {"success", false},
                {"errors", nlohmann::json::array({{
                    {"phase", "server"}, {"type", "InternalError"},
                    {"message", std::string("Compiler failure: ") + e.what()},
                    {"line", 0}, {"col", 0}, {"snippet", ""}}})}};
            res.set_content(out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                            "application/json");
        }
    });

    // ---- AI relay -----------------------------------------------------------
    server.Get("/ai/status", [&](const httplib::Request&, httplib::Response& res) {
        nlohmann::json out = {{"serverKey", !serverKey.empty()}};   // never the key itself
        res.set_content(out.dump(), "application/json");
    });

    server.Post("/ai", [&](const httplib::Request& req, httplib::Response& res) {
        auto fail = [&](int status, const std::string& message) {
            res.status = status;
            nlohmann::json j = {{"error", {{"code", status}, {"message", message}}}};
            res.set_content(j.dump(), "application/json");
        };

        // Only the playground's own page may spend the key. A browser always
        // sends Origin on a cross-site POST, so another website cannot use it.
        if (req.has_header("Origin")) {
            std::string expected = "http://" + req.get_header_value("Host");
            if (req.get_header_value("Origin") != expected)
                return fail(403, "AI requests are only accepted from the playground page itself.");
        }
        if (serverKey.empty())
            return fail(503, "This server has no Gemini key. Set GEMINI_API_KEY or create gemini.key, "
                             "or paste your own key in the playground.");

        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) { return fail(400, "Request body must be JSON: { \"model\", \"prompt\" }."); }
        std::string model = body.value("model", "");
        std::string prompt = body.value("prompt", "");
        if (!lucid::isAllowedModel(model)) return fail(400, "Unknown model id.");
        if (prompt.empty()) return fail(400, "Empty prompt.");
        if (prompt.size() > kMaxPromptBytes) return fail(413, "Prompt too large.");

        auto start = std::chrono::steady_clock::now();
        lucid::AiReply reply = lucid::callGemini(model, prompt, serverKey);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("AI relay  %-18s -> HTTP %d in %.1fs\n", model.c_str(), reply.status, secs);
        std::fflush(stdout);

        res.status = reply.status;
        res.set_content(reply.body, "application/json");
    });

    // Last line of defence: any exception in a handler becomes a JSON 500,
    // never a crashed server.
    server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string what = "unknown error";
        try { if (ep) std::rethrow_exception(ep); }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) {}
        res.status = 500;
        nlohmann::json j = {{"error", {{"code", 500}, {"message", "Server error: " + what}}}};
        res.set_content(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), "application/json");
    });

    // Simple health check.
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::cout << "Lucid playground running at  http://localhost:" << port << "\n";
    std::cout << "Serving web assets from      " << webDir << "\n";
    if (serverKey.empty())
        std::cout << "AI features: visitors must paste their own Gemini key\n"
                     "             (or set GEMINI_API_KEY / create gemini.key and restart)\n";
    else
        std::cout << "AI features: ON for every visitor, using the key from " << keySource << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "error: could not bind to port " << port
                  << " (is it already in use?)\n";
        return 1;
    }
    return 0;
}
