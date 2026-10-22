// ============================================================================
// compiler.cpp — Phase orchestrator for Lucid.
// Wires the six phases together, times each one, and assembles the JSON the
// front-end renders.
// ============================================================================
#include "compiler.hpp"

#include "token.hpp"
#include "error.hpp"
#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "optimizer.hpp"
#include "ircode.hpp"
#include "interpreter.hpp"
#include "bigstack.hpp"

#include <chrono>

namespace lucid {

namespace {
// Wall-clock microseconds since `start`, for the per-phase timing report.
double micros(std::chrono::steady_clock::time_point start) {
    using namespace std::chrono;
    return duration<double, std::micro>(steady_clock::now() - start).count();
}
} // namespace

// Deeper trees than this are rejected before any phase walks them: a long
// enough chain such as 1 + 1 + 1 + ... builds a tree this deep without any
// brackets. No real program comes close.
static constexpr int kMaxTreeDepth = 2000;

static nlohmann::json compileImpl(const std::string& source, const std::string& input,
                                  const std::function<bool(std::string&)>& moreInput = nullptr) {
    using nlohmann::json;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    json timings = json::object();

    ErrorList errors;
    json result = {
        {"tokens", json::array()},
        {"ast", nullptr},
        {"symbolTable", nullptr},
        {"optimized", nullptr},
        {"ir", nullptr},
        {"timings", nullptr},
        {"output", json::array()},
        {"errors", json::array()},
        {"success", false}
    };

    auto finish = [&]() {
        timings["total"] = micros(t0);
        result["timings"] = timings;
        result["errors"] = errors.toJson();
        return result;
    };

    // ---- Phase 1: Lexer ----
    auto ts = Clock::now();
    Lexer lexer(source, errors);
    std::vector<Token> tokens = lexer.tokenize();
    timings["lexer"] = micros(ts);
    for (const auto& t : tokens) result["tokens"].push_back(tokenToJson(t));
    if (!errors.empty()) return finish();

    // ---- Phase 2: Parser ----
    ts = Clock::now();
    Parser parser(tokens, source, errors);
    std::unique_ptr<Program> program = parser.parse();
    timings["parser"] = micros(ts);
    int deepestLine = 0;
    if (treeDepth(*program, deepestLine) > kMaxTreeDepth) {
        errors.add("syntax", "TooDeeplyNested",
                   "This code is nested more than " + std::to_string(kMaxTreeDepth) +
                   " levels deep, for example a very long chain of operators. "
                   "Split it into smaller statements.",
                   deepestLine, 0, sourceLine(source, deepestLine));
        return finish();
    }
    result["ast"] = program->toJson();
    if (!errors.empty()) return finish();

    // ---- Phase 3: Semantic analysis ----
    ts = Clock::now();
    SemanticAnalyzer sema(source, errors);
    result["symbolTable"] = sema.analyze(*program);
    timings["semantic"] = micros(ts);
    if (!errors.empty()) return finish();

    // ---- Phase 4: Optimizer ----
    json astBefore = result["ast"];                 // pre-optimization tree
    int nodesBefore = countJsonNodes(astBefore);
    ts = Clock::now();
    Optimizer optimizer;
    optimizer.optimize(*program);
    timings["optimizer"] = micros(ts);
    json astAfter = program->toJson();
    int nodesAfter = countJsonNodes(astAfter);
    result["optimized"] = {
        {"astBefore", astBefore},
        {"astAfter", astAfter},
        {"nodesBefore", nodesBefore},
        {"nodesAfter", nodesAfter},
        {"removed", nodesBefore - nodesAfter}
    };

    // ---- Phase 5: Intermediate code generation (three-address code) ----
    ts = Clock::now();
    IRGenerator irgen;
    std::vector<Quad> quads = irgen.generate(*program);
    timings["ircode"] = micros(ts);
    result["ir"] = {
        {"code", quadsToJson(quads)},
        {"quads", quads.size()},
        {"temps", irgen.tempCount()},
        {"labels", irgen.labelCount()}
    };

    // ---- Phase 6: Interpreter (runs on the optimized tree) ----
    ts = Clock::now();
    Interpreter interp(source, errors, input, moreInput);
    std::vector<std::string> out = interp.run(*program);
    timings["interpreter"] = micros(ts);
    for (const auto& line : out) result["output"].push_back(line);

    result["success"] = errors.empty();
    return finish();
}

nlohmann::json compile(const std::string& source, const std::string& input,
                       const std::function<bool(std::string&)>& moreInput) {
    nlohmann::json result;
    runOnLargeStack([&] { result = compileImpl(source, input, moreInput); });
    return result;
}

std::string compileToJsonText(const std::string& source, const std::string& input) {
    std::string text;
    runOnLargeStack([&] {
        text = compileImpl(source, input).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    });
    return text;
}

} // namespace lucid
