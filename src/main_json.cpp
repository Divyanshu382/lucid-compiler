// ============================================================================
// main_json.cpp — JSON front-end for Lucid (`lucid-json`), used by the Vercel
// deployment. Reads { "source": "...", "stdin": "..." } on standard input and
// prints exactly the JSON that lucid-server's POST /compile returns, so the
// serverless function (api/compile.js) is a thin wrapper over the same C++
// compiler core.
// ============================================================================
#include <iostream>
#include <iterator>
#include <string>

#include "compiler.hpp"
#include "../third_party/json.hpp"

using nlohmann::json;

static json failure(const std::string& message) {
    return {{"tokens", json::array()}, {"ast", nullptr}, {"symbolTable", nullptr},
            {"optimized", nullptr}, {"output", json::array()}, {"success", false},
            {"errors", json::array({{{"phase", "server"}, {"type", "InternalError"},
                                     {"message", message}, {"line", 0}, {"col", 0},
                                     {"snippet", ""}}})}};
}

int main() {
    std::string body((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::string source, input;
    try {
        auto request = json::parse(body);
        source = request.value("source", "");
        input = request.value("stdin", "");               // what cin reads
    } catch (const std::exception&) {
        std::cout << failure("Body must be JSON: { \"source\": \"...\" }").dump();
        return 1;
    }
    // compileToJsonText runs on a large-stack thread (bigstack.hpp).
    try {
        std::cout << lucid::compileToJsonText(source, input);
    } catch (const std::exception& e) {
        std::cout << failure(std::string("Compiler failure: ") + e.what())
                         .dump(-1, ' ', false, json::error_handler_t::replace);
    }
    return 0;
}
