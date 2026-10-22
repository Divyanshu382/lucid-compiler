// ============================================================================
// main_cli.cpp — command-line front-end for Lucid (`lucidc`).
// Reads a .lucid file, runs the full compiler in-process, and prints tokens,
// a readable AST dump, the symbol table, optimizer before/after counts, any
// errors (with line + snippet), and the program output. Pure C++: no browser,
// no network — this is the deterministic core's proof and fallback demo.
// ============================================================================
#include <fstream>
#include <cstdio>
#include <functional>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <iostream>
#include <sstream>
#include <string>

#include "compiler.hpp"
#include "bigstack.hpp"
#include "../third_party/json.hpp"

using nlohmann::json;

static void printAst(const json& node, const std::string& prefix, bool last, bool root) {
    std::cout << prefix;
    if (!root) std::cout << (last ? "└─ " : "├─ ");
    std::cout << node.value("label", "?") << "  (line " << node.value("line", 0) << ")\n";
    if (!node.contains("children")) return;
    const auto& kids = node["children"];
    std::string childPrefix = prefix;
    if (!root) childPrefix += (last ? "   " : "│  ");
    for (size_t i = 0; i < kids.size(); ++i)
        printAst(kids[i], childPrefix, i + 1 == kids.size(), false);
}

static int runCli(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // the AST tree uses box-drawing characters
#endif
    if (argc < 2) {
        std::cerr << "Usage: lucidc <program> [input-file]\n"
                     "       lucidc <program> < input.txt     (the input is what cin reads)\n";
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "error: cannot open '" << argv[1] << "'\n";
        return 2;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    // The program's input (what cin reads): a second file argument, or else
    // standard input. Standard input is read only when the program executes a
    // cin, a line at a time, so a program without cin never waits for input,
    // and in a terminal you type the values when they are asked for.
    std::string input;
    std::function<bool(std::string&)> moreInput = nullptr;
    if (argc >= 3) {
        std::ifstream inFile(argv[2]);
        if (!inFile) { std::cerr << "error: cannot open input file '" << argv[2] << "'\n"; return 2; }
        std::stringstream ib; ib << inFile.rdbuf(); input = ib.str();
    } else {
        moreInput = [](std::string& line) {
            if (isatty(fileno(stdin))) { std::cerr << "input> "; std::cerr.flush(); }
            return static_cast<bool>(std::getline(std::cin, line));
        };
    }

    json result = lucid::compile(source, input, moreInput);

    std::cout << "=== Lucid ===  " << argv[1] << "\n\n";

    // --- Tokens ---
    std::cout << "--- Tokens (" << result["tokens"].size() << ") ---\n";
    for (const auto& t : result["tokens"]) {
        std::cout << "  " << t.value("line", 0) << ":" << t.value("col", 0)
                  << "  " << t.value("type", "") << "  '" << t.value("value", "") << "'\n";
    }
    std::cout << "\n";

    // --- AST ---
    std::cout << "--- AST ---\n";
    if (!result["ast"].is_null()) printAst(result["ast"], "", true, true);
    else std::cout << "  (not built — earlier phase failed)\n";
    std::cout << "\n";

    // --- Symbol table ---
    std::cout << "--- Symbol Table ---\n";
    if (!result["symbolTable"].is_null()) {
        const auto& st = result["symbolTable"];
        std::cout << "  Variables:\n";
        for (const auto& v : st.value("variables", json::array()))
            std::cout << "    " << v.value("name", "") << " : " << v.value("type", "")
                      << "  [" << v.value("kind", "variable") << " @ " << v.value("scope", "") << "]\n";
        std::cout << "  Functions:\n";
        for (const auto& f : st.value("functions", json::array()))
            std::cout << "    " << f.value("signature", "") << "\n";
    } else {
        std::cout << "  (not built)\n";
    }
    std::cout << "\n";

    // --- Optimizer ---
    std::cout << "--- Optimizer ---\n";
    if (!result["optimized"].is_null()) {
        const auto& o = result["optimized"];
        std::cout << "  nodes before: " << o.value("nodesBefore", 0)
                  << "   after: " << o.value("nodesAfter", 0)
                  << "   removed: " << o.value("removed", 0) << "\n";
    } else {
        std::cout << "  (not run)\n";
    }
    std::cout << "\n";

    // --- Intermediate code ---
    std::cout << "--- Intermediate Code (three-address) ---\n";
    if (!result["ir"].is_null()) {
        const auto& ir = result["ir"];
        for (const auto& q : ir["code"]) {
            std::string op = q.value("op", "");
            bool flushLeft = (op == "label" || op == "func" || op == "endfunc");
            std::printf("  %3d  %s%s\n", q.value("n", 0), flushLeft ? "" : "    ",
                        q.value("text", "").c_str());
        }
        std::cout << "  " << ir.value("quads", 0) << " quadruples, "
                  << ir.value("temps", 0) << " temporaries, "
                  << ir.value("labels", 0) << " labels\n";
    } else {
        std::cout << "  (not generated)\n";
    }
    std::cout << "\n";

    // --- Output ---
    std::cout << "--- Output ---\n";
    for (const auto& line : result["output"])
        std::cout << line.get<std::string>() << "\n";
    if (result["output"].empty()) std::cout << "  (no output)\n";
    std::cout << "\n";

    // --- Errors ---
    std::cout << "--- Errors (" << result["errors"].size() << ") ---\n";
    for (const auto& e : result["errors"]) {
        std::cout << "  [" << e.value("phase", "") << "] " << e.value("type", "")
                  << " (line " << e.value("line", 0) << "): " << e.value("message", "") << "\n";
        std::string snip = e.value("snippet", "");
        if (!snip.empty()) std::cout << "      | " << snip << "\n";
    }
    if (result["errors"].empty()) std::cout << "  (none)\n";
    std::cout << "\n";

    // --- Timing ---
    std::cout << "--- Timing (microseconds) ---\n";
    if (!result["timings"].is_null()) {
        const char* order[] = {"lexer", "parser", "semantic", "optimizer", "ircode", "interpreter", "total"};
        for (const char* k : order) {
            if (!result["timings"].contains(k)) continue;
            std::printf("  %-12s %10.1f\n", k, result["timings"][k].get<double>());
        }
    }

    return result.value("success", false) ? 0 : 1;
}

int main(int argc, char** argv) {
    // The AST printer and every compiler phase recurse over the tree, so run
    // everything on a large stack (the Windows main thread has only 1 MB).
    int code = 0;
    lucid::runOnLargeStack([&] { code = runCli(argc, argv); });
    return code;
}
