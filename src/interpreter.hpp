// ============================================================================
// interpreter.hpp — Phase 5 of Lucid.
// Input : (optimized) Program AST — assumed already type-checked
// Output: the text the program prints, plus any runtime error.
//
// A tree-walking evaluator. Values are std::variant<int,string,bool>. Blocks
// push/pop scopes; a function call runs in an isolated scope chain (params
// only) with a call-stack depth guard so runaway recursion can't crash the
// server. Runtime error: division / modulo by zero.
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "ast.hpp"
#include "error.hpp"

namespace lucid {

using Value = std::variant<int, std::string, bool>;

std::string valueToString(const Value& v);

class Interpreter {
public:
    Interpreter(const std::string& source, ErrorList& errors);

    // Run the program. Returns everything print() emitted, in order.
    std::vector<std::string> run(Program& program);

private:
    // Thrown by `return` to unwind to the active call.
    struct ReturnSignal { Value value; };
    // Thrown by `break` / `continue`; caught by the innermost loop.
    struct BreakSignal {};
    struct ContinueSignal {};
    // Thrown on a runtime fault (e.g. division by zero).
    struct RuntimeFault { std::string message; int line; };

    using Scope = std::unordered_map<std::string, Value>;

    const std::string& source_;
    ErrorList& errors_;
    std::vector<Scope> scopes_;
    std::unordered_map<std::string, FuncDecl*> funcs_;
    std::vector<std::string> output_;
    int callDepth_ = 0;
    static constexpr int kMaxCallDepth = 10000;   // safe on the 256 MB stack (bigstack.hpp)

    // Execution budget. Every loop iteration and every function call costs one
    // step. The playground recompiles on each keystroke, so a half-typed loop
    // such as `while (i < 5) { print(i); }` must stop instead of hanging.
    long long steps_ = 0;
    static constexpr long long kMaxSteps = 2000000;
    static constexpr size_t kMaxOutputLines = 10000;
    // Memory guards: a loop that doubles a string would otherwise try to
    // allocate terabytes and take the whole machine down with it.
    static constexpr size_t kMaxStringLength = 1000000;
    static constexpr size_t kMaxOutputBytes = 2000000;
    size_t outputBytes_ = 0;
    void spendStep(int line);

    void define(const std::string& name, const Value& v);
    bool assign(const std::string& name, const Value& v);
    bool lookup(const std::string& name, Value& out) const;

    void execBlock(Block* block);          // runs in a fresh child scope
    void execStatement(Node* stmt);
    Value evalExpr(Node* expr);
    Value evalBinary(BinaryOp* bin);
    Value callFunction(Call* call);

    static int    asInt(const Value& v);
    static bool   asBool(const Value& v);
    static const std::string& asStr(const Value& v);
};

} // namespace lucid
