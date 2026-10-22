// ============================================================================
// semantic.hpp — Phase 3 of Lucid.
// Input : Program AST
// Output: type-checked AST (unchanged shape) + a symbol-table snapshot (JSON)
//         + semantic errors into an ErrorList.
//
// Walks the tree with a stack of scopes, tracks variable types and function
// signatures, and reports *every* semantic error in one pass (using an
// internal Error type to stop cascades). Undeclared names get a
// Levenshtein-based "did you mean ...?" suggestion.
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "ast.hpp"
#include "error.hpp"
#include "../third_party/json.hpp"

namespace lucid {

// Static types in Lucid. `Error` is an internal marker used to suppress
// cascading messages once a sub-expression is already known bad.
enum class Ty { Int, String, Bool, Void, Error };

const char* tyName(Ty t);

class SemanticAnalyzer {
public:
    SemanticAnalyzer(const std::string& source, ErrorList& errors);

    // Type-check the whole program. Returns the symbol-table snapshot JSON.
    nlohmann::json analyze(Program& program);

private:
    struct FuncSig {
        std::vector<Ty> paramTypes;
        std::vector<std::string> paramNames;
        Ty returnType;
        std::string returnTypeName;
        int line;
    };
    struct VarEntry { Ty type; bool isConst = false; };

    const std::string& source_;
    ErrorList& errors_;

    std::vector<std::unordered_map<std::string, VarEntry>> scopes_;
    std::vector<std::string> scopeLabels_;
    std::unordered_map<std::string, FuncSig> funcs_;
    std::vector<Ty> returnStack_;       // enclosing function return types
    int loopDepth_ = 0;                 // > 0 inside a loop body: break/continue allowed

    // Snapshot data for the UI.
    nlohmann::json variablesSnapshot_ = nlohmann::json::array();
    nlohmann::json functionsSnapshot_ = nlohmann::json::array();

    // scope management
    void pushScope(const std::string& label);
    void popScope();
    bool declareVar(const std::string& name, Ty type, bool isConst = false);   // false if redeclared here
    const VarEntry* findVar(const std::string& name) const;
    bool lookupVar(const std::string& name, Ty& out) const;
    std::vector<std::string> knownVarNames() const;

    // checking
    void checkStatement(Node* stmt);
    void checkBlock(Block* block, bool newScope, const std::string& label);
    void checkFuncBody(FuncDecl* fn);
    Ty checkExpr(Node* expr);
    Ty checkBinary(BinaryOp* bin);
    Ty checkCall(Call* call);
    // C++'s implicit int <-> bool conversions, made explicit: wraps the
    // expression in `slot` in a Cast node. False if no such conversion exists.
    bool coerce(NodePtr& slot, Ty from, Ty to);
    Ty checkCondition(NodePtr& cond, const std::string& keyword, int line);

    void reportTypeError(const std::string& type, const std::string& msg, int line);
    static Ty tyFromName(const std::string& name);
};

// Public helper so tests / other phases can reuse the suggestion logic.
int levenshtein(const std::string& a, const std::string& b);
std::string suggestName(const std::string& target, const std::vector<std::string>& candidates);

} // namespace lucid
