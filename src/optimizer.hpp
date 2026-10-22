// ============================================================================
// optimizer.hpp — Phase 4 of Lucid.
// Input : type-checked Program AST
// Output: an equivalent, smaller Program AST (mutated in place).
//
// Two transformations, applied together bottom-up:
//   1. Constant folding      — 2 + 3 -> 5, "a"+"b" -> "ab", !true -> false,
//                              1 < 2 -> true, etc.
//   2. Dead-code elimination — drop if(false) branches, unwrap if(true),
//                              remove while(false), choose the branch of a
//                              constant ?:, and delete any statements that
//                              follow a return, break or continue in the same
//                              block.
// Node counts before/after are reported by the compiler for the UI.
// ============================================================================
#pragma once

#include <vector>
#include "ast.hpp"

namespace lucid {

class Optimizer {
public:
    // Rewrite the program in place.
    void optimize(Program& program);

private:
    void optStmtList(std::vector<NodePtr>& stmts);
    NodePtr optStmt(NodePtr stmt);     // returns replacement, or nullptr to drop
    NodePtr foldExpr(NodePtr expr);
};

} // namespace lucid
