// ============================================================================
// ircode.hpp — Intermediate code generation for Lucid.
// Phase: between the Optimizer and the Interpreter.
// Input : the optimized AST (Program)
// Output: a flat list of three-address-code quadruples
//
// Each quadruple is (op, arg1, arg2, result): at most one operator and three
// addresses, the form in which a compiler back end would see the program.
// Expressions are flattened into compiler temporaries t0, t1, ...; control
// flow (if / while / for) becomes conditional and unconditional jumps
// between labels L0, L1, ...
//
// The generated code is presented for inspection and measured; execution is
// still performed by the tree-walking Interpreter.
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "ast.hpp"
#include "../third_party/json.hpp"

namespace lucid {

struct Quad {
    std::string op;
    std::string arg1;
    std::string arg2;
    std::string result;
    std::string text() const;   // human-readable form, e.g. "t0 = x + 10"
};

class IRGenerator {
public:
    std::vector<Quad> generate(const Program& program);
    int tempCount() const { return temps_; }
    int labelCount() const { return labels_; }

private:
    std::vector<Quad> code_;
    int temps_ = 0;
    int labels_ = 0;

    // Jump targets of the enclosing loops: `continue` goes to the first label,
    // `break` to the second.
    struct LoopLabels { std::string continueTo, breakTo; };
    std::vector<LoopLabels> loops_;

    // Scopes, mirroring the semantic analyser. A declaration that hides a
    // variable from an enclosing scope gets a distinct IR name (x, x.1, ...)
    // so the two variables never share storage in the three-address code;
    // so does a variable spelled like a temporary (t0 becomes t0.1).
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::unordered_map<std::string, int> renames_;
    std::string declareName(const std::string& name);   // IR name for a new variable
    std::string resolveName(const std::string& name) const;

    std::string newTemp()  { return "t" + std::to_string(temps_++); }
    std::string newLabel() { return "L" + std::to_string(labels_++); }
    void emit(const std::string& op, const std::string& a1 = "",
              const std::string& a2 = "", const std::string& res = "") {
        code_.push_back(Quad{op, a1, a2, res});
    }

    void genStmt(const Node& n);
    void genBlock(const Block& b);
    std::string genExpr(const Node& n);   // returns the address holding the value
};

nlohmann::json quadsToJson(const std::vector<Quad>& code);

} // namespace lucid
