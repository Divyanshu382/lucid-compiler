// ============================================================================
// optimizer.cpp — Phase 4 of Lucid.  Constant folding + dead code elimination.
// Runs after semantic analysis (so the tree is well-typed) and before the
// interpreter (so it executes the smaller, equivalent program).
// ============================================================================
#include "optimizer.hpp"
#include "intmath.hpp"

namespace lucid {

// ---- literal helpers -----------------------------------------------------
static bool isInt(const Node* n)  { return n && n->kind == NodeKind::IntLiteral; }
static bool isBool(const Node* n) { return n && n->kind == NodeKind::BoolLiteral; }
static bool isStr(const Node* n)  { return n && n->kind == NodeKind::StringLiteral; }

static int  intVal(const Node* n)  { return static_cast<const IntLiteral*>(n)->value; }
static bool boolVal(const Node* n) { return static_cast<const BoolLiteral*>(n)->value; }
static const std::string& strVal(const Node* n) { return static_cast<const StringLiteral*>(n)->value; }

static NodePtr makeInt(int v, int line) {
    auto n = std::make_unique<IntLiteral>(line); n->value = v; return n;
}
static NodePtr makeBool(bool v, int line) {
    auto n = std::make_unique<BoolLiteral>(line); n->value = v; return n;
}
static NodePtr makeStr(std::string v, int line) {
    auto n = std::make_unique<StringLiteral>(line); n->value = std::move(v); return n;
}

// ---- entry ---------------------------------------------------------------
void Optimizer::optimize(Program& program) {
    optStmtList(program.statements);
}

void Optimizer::optStmtList(std::vector<NodePtr>& stmts) {
    std::vector<NodePtr> out;
    for (auto& s : stmts) {
        NodePtr opt = optStmt(std::move(s));
        if (!opt) continue;                       // dropped (dead code)
        bool jumpsAway = opt->kind == NodeKind::Return || opt->kind == NodeKind::Break ||
                         opt->kind == NodeKind::Continue;
        out.push_back(std::move(opt));
        if (jumpsAway) break;                     // everything after it is unreachable
    }
    stmts = std::move(out);
}

NodePtr Optimizer::optStmt(NodePtr stmt) {
    switch (stmt->kind) {
        case NodeKind::VarDecl: {
            auto* d = static_cast<VarDecl*>(stmt.get());
            d->init = foldExpr(std::move(d->init));
            return stmt;
        }
        case NodeKind::Assign: {
            auto* a = static_cast<Assign*>(stmt.get());
            a->value = foldExpr(std::move(a->value));
            return stmt;
        }
        case NodeKind::Print: {
            auto* p = static_cast<Print*>(stmt.get());
            for (auto& a : p->args) a = foldExpr(std::move(a));
            return stmt;
        }
        case NodeKind::Return: {
            auto* r = static_cast<Return*>(stmt.get());
            r->value = foldExpr(std::move(r->value));
            return stmt;
        }
        case NodeKind::If: {
            auto* i = static_cast<If*>(stmt.get());
            i->cond = foldExpr(std::move(i->cond));
            optStmtList(i->thenBlock->statements);
            if (i->elseBlock) optStmtList(i->elseBlock->statements);

            if (isBool(i->cond.get())) {           // condition is a known constant
                if (boolVal(i->cond.get()))
                    return std::move(i->thenBlock);       // keep only the taken branch
                if (i->elseBlock)
                    return std::move(i->elseBlock);
                return nullptr;                            // if(false) with no else -> gone
            }
            return stmt;
        }
        case NodeKind::While: {
            auto* w = static_cast<While*>(stmt.get());
            w->cond = foldExpr(std::move(w->cond));
            optStmtList(w->body->statements);
            if (w->step) w->step = optStmt(std::move(w->step));
            // while(false) never runs. A do-while(false) still runs its body
            // once, so it is kept.
            if (!w->doWhile && isBool(w->cond.get()) && !boolVal(w->cond.get()))
                return nullptr;
            return stmt;
        }
        case NodeKind::FuncDecl: {
            auto* fn = static_cast<FuncDecl*>(stmt.get());
            optStmtList(fn->body->statements);
            return stmt;
        }
        case NodeKind::Block: {
            auto* b = static_cast<Block*>(stmt.get());
            optStmtList(b->statements);
            return stmt;
        }
        case NodeKind::Call: {
            auto* c = static_cast<Call*>(stmt.get());
            for (auto& a : c->args) a = foldExpr(std::move(a));
            return stmt;
        }
        default:
            return stmt;
    }
}

NodePtr Optimizer::foldExpr(NodePtr expr) {
    if (!expr) return expr;

    if (expr->kind == NodeKind::UnaryOp) {
        auto* u = static_cast<UnaryOp*>(expr.get());
        u->operand = foldExpr(std::move(u->operand));
        Node* o = u->operand.get();
        if (u->op == "!" && isBool(o)) return makeBool(!boolVal(o), u->line);
        int neg;
        if (u->op == "-" && isInt(o) && checkedNeg(intVal(o), neg)) return makeInt(neg, u->line);
        return expr;
    }

    if (expr->kind == NodeKind::Call) {
        auto* c = static_cast<Call*>(expr.get());
        for (auto& a : c->args) a = foldExpr(std::move(a));
        return expr;
    }

    // A conversion of a constant is folded: (int) true -> 1, (bool) 5 -> true.
    if (expr->kind == NodeKind::Cast) {
        auto* c = static_cast<Cast*>(expr.get());
        c->operand = foldExpr(std::move(c->operand));
        Node* o = c->operand.get();
        if (c->type == "string") return std::move(c->operand);   // string("abc") is just "abc"
        if (isInt(o) || isBool(o)) {
            int n = isInt(o) ? intVal(o) : (boolVal(o) ? 1 : 0);
            return c->type == "bool" ? makeBool(n != 0, c->line) : makeInt(n, c->line);
        }
        return expr;
    }

    // c ? a : b with a constant condition becomes just the chosen branch.
    if (expr->kind == NodeKind::Conditional) {
        auto* c = static_cast<Conditional*>(expr.get());
        c->cond = foldExpr(std::move(c->cond));
        c->thenExpr = foldExpr(std::move(c->thenExpr));
        c->elseExpr = foldExpr(std::move(c->elseExpr));
        if (isBool(c->cond.get()))
            return boolVal(c->cond.get()) ? std::move(c->thenExpr) : std::move(c->elseExpr);
        return expr;
    }

    if (expr->kind != NodeKind::BinaryOp) return expr;   // literals, identifiers

    auto* b = static_cast<BinaryOp*>(expr.get());
    b->left  = foldExpr(std::move(b->left));
    b->right = foldExpr(std::move(b->right));
    const std::string& op = b->op;
    Node* L = b->left.get();
    Node* R = b->right.get();
    int line = b->line;

    // Boolean short-circuit folding on a constant LEFT operand (side-effect safe,
    // because && / || never evaluate the right side when the left decides it).
    if ((op == "&&" || op == "||") && isBool(L)) {
        bool lv = boolVal(L);
        if (op == "&&") return lv ? std::move(b->right) : makeBool(false, line);
        else            return lv ? makeBool(true, line) : std::move(b->right);
    }

    // Integer arithmetic on two int literals.
    if (isInt(L) && isInt(R)) {
        int a = intVal(L), c = intVal(R), v = 0;
        // Fold only when the result fits; otherwise keep the expression so the
        // runtime reports the overflow (and division by zero) with its line.
        if (op == "+") return checkedAdd(a, c, v) ? makeInt(v, line) : std::move(expr);
        if (op == "-") return checkedSub(a, c, v) ? makeInt(v, line) : std::move(expr);
        if (op == "*") return checkedMul(a, c, v) ? makeInt(v, line) : std::move(expr);
        if (op == "/") { if (c == 0 || !checkedDiv(a, c, v)) return expr; return makeInt(v, line); }
        if (op == "%") { if (c == 0) return expr; return makeInt(safeMod(a, c), line); }
        if (op == ">>" && c >= 0 && c <= 31) return makeInt(a >> c, line);
        if (op == "<<" && c >= 0 && c <= 31 && a >= 0 && fitsInt(static_cast<long long>(a) * (1LL << c)))
            return makeInt(static_cast<int>(static_cast<long long>(a) * (1LL << c)), line);
        if (op == "<")  return makeBool(a <  c, line);
        if (op == ">")  return makeBool(a >  c, line);
        if (op == "<=") return makeBool(a <= c, line);
        if (op == ">=") return makeBool(a >= c, line);
        if (op == "==") return makeBool(a == c, line);
        if (op == "!=") return makeBool(a != c, line);
    }

    // String concatenation / comparison on two string literals.
    if (isStr(L) && isStr(R)) {
        if (op == "+")  return makeStr(strVal(L) + strVal(R), line);
        if (op == "==") return makeBool(strVal(L) == strVal(R), line);
        if (op == "!=") return makeBool(strVal(L) != strVal(R), line);
    }

    // Boolean literal comparison / logic on two bool literals.
    if (isBool(L) && isBool(R)) {
        bool a = boolVal(L), c = boolVal(R);
        if (op == "==") return makeBool(a == c, line);
        if (op == "!=") return makeBool(a != c, line);
        if (op == "&&") return makeBool(a && c, line);
        if (op == "||") return makeBool(a || c, line);
    }

    return expr;
}

} // namespace lucid
