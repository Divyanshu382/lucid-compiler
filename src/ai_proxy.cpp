// ============================================================================
// ai_proxy.cpp — server-side relay to the Gemini API (see ai_proxy.hpp).
// Works on macOS / Linux (posix_spawn) and Windows 10+ (CreateProcess); both
// run the system curl and pass it the request through its stdin config.
// ============================================================================
#include "ai_proxy.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <mutex>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <spawn.h>
  #include <sys/wait.h>
  #include <unistd.h>
  extern char** environ;
#endif

#include "../third_party/json.hpp"

namespace lucid {

namespace {

const char* kStatusMarker = "__LUCID_HTTP_STATUS__";

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// Quote a value for a curl config file: inside double quotes curl unescapes
// \\ and \". The JSON body is a single line, so nothing else needs escaping.
std::string curlQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}

AiReply relayError(int status, const std::string& message) {
    nlohmann::json j = {{"error", {{"code", status}, {"message", message}}}};
    return AiReply{status, j.dump()};
}

// A started curl must never inherit another request's pipe (it would keep that
// pipe open), so pipe creation and process start happen under one lock.
std::mutex spawnMutex;

#ifdef _WIN32
// Windows: run `curl.exe --config -` with redirected stdin/stdout.
bool runCurl(const std::string& config, std::string& output, int& exitCode) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    PROCESS_INFORMATION pi{};
    {
        std::lock_guard<std::mutex> lock(spawnMutex);
        if (!CreatePipe(&inRead, &inWrite, &sa, 0)) return false;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) { CloseHandle(inRead); CloseHandle(inWrite); return false; }
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);   // our ends stay private
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        char cmd[] = "curl.exe --config -";
        BOOL ok = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) { CloseHandle(inWrite); CloseHandle(outRead); return false; }
    }

    size_t done = 0;
    DWORD written = 0;
    while (done < config.size() &&
           WriteFile(inWrite, config.data() + done, static_cast<DWORD>(config.size() - done), &written, nullptr))
        done += written;
    CloseHandle(inWrite);   // curl reads its config to end-of-file before it starts

    char buf[8192];
    DWORD got = 0;
    while (ReadFile(outRead, buf, sizeof buf, &got, nullptr) && got > 0) output.append(buf, got);
    CloseHandle(outRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
#else
// macOS / Linux: run `curl --config -` with posix_spawn and two pipes.
bool runCurl(const std::string& config, std::string& output, int& exitCode) {
    int toCurl[2], fromCurl[2];
    pid_t pid = 0;
    {
        std::lock_guard<std::mutex> lock(spawnMutex);
        if (::pipe(toCurl) != 0) return false;
        if (::pipe(fromCurl) != 0) { ::close(toCurl[0]); ::close(toCurl[1]); return false; }
        for (int fd : {toCurl[0], toCurl[1], fromCurl[0], fromCurl[1]})
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, toCurl[0], STDIN_FILENO);     // config in
        posix_spawn_file_actions_adddup2(&actions, fromCurl[1], STDOUT_FILENO);  // reply out
        char arg0[] = "curl", arg1[] = "--config", arg2[] = "-";
        char* argv[] = {arg0, arg1, arg2, nullptr};
        int rc = posix_spawnp(&pid, "curl", &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(toCurl[0]);
        ::close(fromCurl[1]);
        if (rc != 0) { ::close(toCurl[1]); ::close(fromCurl[0]); return false; }
    }

    size_t done = 0;
    while (done < config.size()) {
        ssize_t n = ::write(toCurl[1], config.data() + done, config.size() - done);
        if (n < 0) { if (errno == EINTR) continue; break; }
        done += static_cast<size_t>(n);
    }
    ::close(toCurl[1]);   // curl reads its config to end-of-file before it starts

    char buf[8192];
    for (;;) {
        ssize_t n = ::read(fromCurl[0], buf, sizeof buf);
        if (n > 0) { output.append(buf, static_cast<size_t>(n)); continue; }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    ::close(fromCurl[0]);

    int waitStatus = 0;
    while (::waitpid(pid, &waitStatus, 0) < 0 && errno == EINTR) {}
    exitCode = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
    return true;
}
#endif

} // namespace

std::string loadServerKey(const std::string& envName, const std::string& keyFile, std::string& source) {
    if (const char* env = std::getenv(envName.c_str())) {
        std::string k = trim(env);
        if (!k.empty()) { source = "environment variable " + envName; return k; }
    }
    std::ifstream in(keyFile);
    std::string line;
    if (in && std::getline(in, line)) {
        std::string k = trim(line);
        if (!k.empty()) { source = keyFile; return k; }
    }
    source = "";
    return "";
}

bool isAllowedModel(const std::string& model) {
    bool known = model.rfind("gemini-", 0) == 0 || model.rfind("deepseek-", 0) == 0;
    if (model.size() < 8 || model.size() > 64 || !known) return false;
    for (char c : model) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// POSTs `body` to `url` with the given auth header, through curl's stdin config.
static AiReply postJson(const std::string& url, const std::string& authHeader, const std::string& body) {
    std::string config =
        "url = " + curlQuote(url) + "\n"
        "header = " + curlQuote("Content-Type: application/json") + "\n"
        "header = " + curlQuote(authHeader) + "\n"
        "data-binary = " + curlQuote(body) + "\n"
        "silent\n"
        "show-error\n"
        "max-time = 28\n"
        "write-out = \"\\n" + std::string(kStatusMarker) + "%{http_code}\"\n";

    std::string output;
    int exitCode = 0;
    if (!runCurl(config, output, exitCode))
        return relayError(502, "The server could not start curl; is it installed?");

    size_t at = output.rfind(kStatusMarker);
    int status = 0;
    if (at != std::string::npos) {
        try { status = std::stoi(output.substr(at + std::string(kStatusMarker).size())); } catch (...) {}
    }
    if (status == 0) {
        return relayError(502, exitCode == 28
            ? "The AI service did not answer within 28 seconds."
            : "The server could not reach the AI service (curl exit code " + std::to_string(exitCode) +
              "). Check the internet connection.");
    }
    std::string reply = output.substr(0, at);
    if (!reply.empty() && reply.back() == '\n') reply.pop_back();
    return AiReply{status, reply};
}

AiReply callGemini(const std::string& model, const std::string& prompt, const std::string& apiKey) {
    nlohmann::json payload = {{"contents", {{{"parts", {{{"text", prompt}}}}}}}};
    return postJson("https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent",
                    "x-goog-api-key: " + apiKey, payload.dump());
}

AiReply callDeepSeek(const std::string& model, const std::string& prompt, const std::string& apiKey) {
    nlohmann::json payload = {{"model", model},
                              {"messages", {{{"role", "user"}, {"content", prompt}}}}};
    AiReply r = postJson("https://api.deepseek.com/chat/completions",
                         "Authorization: Bearer " + apiKey, payload.dump());
    if (r.status != 200) return r;                        // errors pass through unchanged
    try {
        auto j = nlohmann::json::parse(r.body);
        std::string text = j.at("choices").at(0).at("message").at("content").get<std::string>();
        nlohmann::json asGemini = {{"candidates", {{{"content", {{"parts", {{{"text", text}}}}}}}}}};
        return AiReply{200, asGemini.dump()};
    } catch (const std::exception&) {
        return relayError(502, "DeepSeek returned a reply in an unexpected format.");
    }
}

} // namespace lucid
