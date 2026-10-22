// ============================================================================
// semantic.cpp — Phase 3 of Lucid.  Type checking + symbol table.
// Reports all semantic errors in a single pass. See error taxonomy in the
// build brief: undeclared/redeclared variables, type mismatches, non-boolean
// conditions, unknown functions, wrong argument count/types, return mismatch.
// ============================================================================
#include "semantic.hpp"
#include <algorithm>

namespace lucid {

const char* tyName(Ty t) {
    switch (t) {
        case Ty::Int:    return "int";
        case Ty::String: return "string";
        case Ty::Bool:   return "bool";
        case Ty::Void:   return "void";
        case Ty::Error:  return "<error>";
    }
    return "<error>";
}

Ty SemanticAnalyzer::tyFromName(const std::string& name) {
    if (name == "int")    return Ty::Int;
    if (name == "string") return Ty::String;
    if (name == "bool")   return Ty::Bool;
    if (name == "void")   return Ty::Void;
    return Ty::Error;
}

// ---- Levenshtein edit distance + nearest-name suggestion -----------------
int levenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

std::string suggestName(const std::string& target, const std::vector<std::string>& candidates) {
    std::string best;
    int bestDist = 1 << 30;
    for (const auto& c : candidates) {
        if (c == target) continue;
        int d = levenshtein(target, c);
        if (d < bestDist) { bestDist = d; best = c; }
    }
    // Only suggest when the names are genuinely close.
    int threshold = std::max(2, static_cast<int>(target.size()) / 3);
    if (!best.empty() && bestDist <= threshold) return best;
    return "";
}


// ---- setup ---------------------------------------------------------------
SemanticAnalyzer::SemanticAnalyzer(const std::string& source, ErrorList& errors)
    : source_(source), errors_(errors) {}

void SemanticAnalyzer::reportTypeError(const std::string& type, const std::string& msg, int line) {
    errors_.add("semantic", type, msg, line, 0, sourceLine(source_, line));
}

void SemanticAnalyzer::pushScope(const std::string& label) {
    scopes_.emplace_back();
    scopeLabels_.push_back(label);
}

void SemanticAnalyzer::popScope() {
    scopes_.pop_back();
    scopeLabels_.pop_back();
}

bool SemanticAnalyzer::declareVar(const std::string& name, Ty type, bool isConst) {
    auto& scope = scopes_.back();
    if (scope.count(name)) return false;   // already declared in this scope
    scope[name] = VarEntry{type, isConst};
    return true;
}

const SemanticAnalyzer::VarEntry* SemanticAnalyzer::findVar(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

bool SemanticAnalyzer::coerce(NodePtr& slot, Ty from, Ty to) {
    if (from == to || from == Ty::Error || to == Ty::Error) return true;
    bool numeric = (from == Ty::Int || from == Ty::Bool) && (to == Ty::Int || to == Ty::Bool);
    if (!numeric) return false;
    auto cast = std::make_unique<Cast>(slot->line);
    cast->type = tyName(to);
    cast->operand = std::move(slot);
    slot = std::move(cast);
    return true;
}

// if / while / do / for / ?: conditions. As in C++, an int is accepted
// (non-zero means true) and converted; a string is not.
Ty SemanticAnalyzer::checkCondition(NodePtr& cond, const std::string& keyword, int line) {
    Ty c = checkExpr(cond.get());
    if (c == Ty::Error) return c;
    if (!coerce(cond, c, Ty::Bool)) {
        reportTypeError("ConditionNotBoolean",
            "The condition of '" + keyword + "' must be a bool or an int, but it is " +
            std::string(tyName(c)) + "." + (c == Ty::String ? " Compare the string instead, e.g. (s != \"\")." : ""), line);
        return Ty::Error;
    }
    return Ty::Bool;
}

bool SemanticAnalyzer::lookupVar(const std::string& name, Ty& out) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) { out = found->second.type; return true; }
    }
    return false;
}

std::vector<std::string> SemanticAnalyzer::knownVarNames() const {
    std::vector<std::string> names;
    for (const auto& scope : scopes_)
        for (const auto& kv : scope) names.push_back(kv.first);
    return names;
}

// ---- top level -----------------------------------------------------------
nlohmann::json SemanticAnalyzer::analyze(Program& program) {
    pushScope("global");

    // Pass 1: hoist top-level function signatures so calls may precede decls.
    for (auto& stmt : program.statements) {
        if (stmt->kind == NodeKind::FuncDecl) {
            auto* fn = static_cast<FuncDecl*>(stmt.get());
            if (funcs_.count(fn->name)) {
                reportTypeError("Redeclaration",
                    "Function '" + fn->name + "' is already defined.", fn->line);
                continue;
            }
            FuncSig sig;
            for (const auto& p : fn->params) {
                sig.paramTypes.push_back(tyFromName(p.type));
                sig.paramNames.push_back(p.name);
            }
            sig.returnType = tyFromName(fn->returnType);
            sig.returnTypeName = fn->returnType;
            sig.line = fn->line;
            funcs_[fn->name] = sig;

            nlohmann::json params = nlohmann::json::array();
            for (const auto& p : fn->params)
                params.push_back({{"name", p.name}, {"type", p.type}});
            functionsSnapshot_.push_back({
                {"name", fn->name},
                {"returnType", fn->returnType},
                {"params", params},
                {"signature", fn->signature()},
                {"scope", "global"},
                {"line", fn->line}
            });
        }
    }

    // Pass 2: check every statement (function bodies included).
    for (auto& stmt : program.statements) checkStatement(stmt.get());

    popScope();

    return nlohmann::json{
        {"variables", variablesSnapshot_},
        {"functions", functionsSnapshot_}
    };
}

void SemanticAnalyzer::checkBlock(Block* block, bool newScope, const std::string& label) {
    if (newScope) pushScope(label);
    for (auto& s : block->statements) checkStatement(s.get());
    if (newScope) popScope();
}

void SemanticAnalyzer::checkFuncBody(FuncDecl* fn) {
    // As in C++, a function sees its parameters, its own locals, and the
    // global variables declared above it in the file (functions are only
    // declared at the top level, so the scope chain here is just the global
    // scope, holding exactly the globals declared so far).
    pushScope("func " + fn->name);
    for (const auto& p : fn->params) {
        Ty pty = tyFromName(p.type);
        if (!declareVar(p.name, pty)) {
            reportTypeError("Redeclaration",
                "Parameter '" + p.name + "' is declared twice in '" + fn->name + "'.", fn->line);
        }
        variablesSnapshot_.push_back({
            {"name", p.name}, {"type", p.type},
            {"scope", "func " + fn->name}, {"kind", "parameter"}, {"line", fn->line}
        });
    }
    returnStack_.push_back(tyFromName(fn->returnType));
    int savedLoopDepth = loopDepth_;
    loopDepth_ = 0;                      // break/continue cannot leave a function
    // Body reuses the function scope (params + locals share it).
    checkBlock(fn->body.get(), /*newScope=*/false, "");
    loopDepth_ = savedLoopDepth;
    returnStack_.pop_back();
    popScope();
}

void SemanticAnalyzer::checkStatement(Node* stmt) {
    switch (stmt->kind) {
        case NodeKind::VarDecl: {
            auto* d = static_cast<VarDecl*>(stmt);
            Ty declTy = tyFromName(d->declType);
            Ty initTy = checkExpr(d->init.get());
            if (!coerce(d->init, initTy, declTy)) {
                reportTypeError("TypeMismatch",
                    "Cannot initialize '" + d->name + "' of type " + d->declType +
                    " with a value of type " + tyName(initTy) + ".", d->line);
            }
            if (!declareVar(d->name, declTy, d->isConst)) {
                reportTypeError("Redeclaration",
                    "Variable '" + d->name + "' is already declared in this scope.", d->line);
            }
            variablesSnapshot_.push_back({
                {"name", d->name}, {"type", d->declType},
                {"scope", scopeLabels_.back()}, {"kind", d->isConst ? "constant" : "variable"}, {"line", d->line}
            });
            break;
        }
        case NodeKind::Assign: {
            auto* a = static_cast<Assign*>(stmt);
            Ty varTy;
            if (!lookupVar(a->name, varTy)) {
                std::string hint = suggestName(a->name, knownVarNames());
                std::string msg = "Assignment to undeclared variable '" + a->name + "'.";
                if (!hint.empty()) msg += " Did you mean '" + hint + "'?";
                reportTypeError("UndeclaredVariable", msg, a->line);
                varTy = Ty::Error;
            }
            Ty valTy = checkExpr(a->value.get());
            if (const VarEntry* v = findVar(a->name); v && v->isConst) {
                reportTypeError("AssignToConst", "Cannot assign to '" + a->name + "' because it is const.", a->line);
            } else if (varTy != Ty::Error && valTy != Ty::Error && !coerce(a->value, valTy, varTy)) {
                reportTypeError("TypeMismatch",
                    "Cannot assign a value of type " + std::string(tyName(valTy)) +
                    " to '" + a->name + "' of type " + tyName(varTy) + ".", a->line);
            }
            break;
        }
        case NodeKind::If: {
            auto* i = static_cast<If*>(stmt);
            checkCondition(i->cond, "if", i->line);
            checkBlock(i->thenBlock.get(), true, "if");
            if (i->elseBlock) checkBlock(i->elseBlock.get(), true, "else");
            break;
        }
        case NodeKind::While: {
            auto* w = static_cast<While*>(stmt);
            const char* kw = w->doWhile ? "do-while" : (w->step ? "for" : "while");
            checkCondition(w->cond, kw, w->line);
            ++loopDepth_;
            checkBlock(w->body.get(), true, kw);
            --loopDepth_;
            if (w->step) checkStatement(w->step.get());   // for-loop update
            break;
        }
        case NodeKind::Break:
        case NodeKind::Continue: {
            if (loopDepth_ == 0) {
                bool isBreak = stmt->kind == NodeKind::Break;
                reportTypeError(isBreak ? "BreakOutsideLoop" : "ContinueOutsideLoop",
                    std::string("'") + (isBreak ? "break" : "continue") +
                    "' can only be used inside a loop.", stmt->line);
            }
            break;
        }
        case NodeKind::Print: {
            auto* p = static_cast<Print*>(stmt);
            for (auto& a : p->args) {   // any well-typed value may be printed
                if (checkExpr(a.get()) == Ty::Void)
                    reportTypeError("VoidValue",
                        "Cannot print the result of a void function: it returns nothing.", p->line);
            }
            break;
        }
        case NodeKind::Return: {
            auto* r = static_cast<Return*>(stmt);
            Ty valTy = r->value ? checkExpr(r->value.get()) : Ty::Void;
            if (returnStack_.empty()) {
                reportTypeError("ReturnOutsideFunction",
                    "'return' can only appear inside a function.", r->line);
            } else {
                Ty want = returnStack_.back();
                if (want == Ty::Void && r->value) {
                    reportTypeError("ReturnTypeMismatch",
                        "A void function cannot return a value; use plain 'return;'.", r->line);
                } else if (want != Ty::Void && want != Ty::Error && !r->value) {
                    reportTypeError("MissingReturnValue",
                        "This function must return a " + std::string(tyName(want)) +
                        " value, but 'return;' gives none.", r->line);
                } else if (valTy != Ty::Error && want != Ty::Error && !coerce(r->value, valTy, want)) {
                    reportTypeError("ReturnTypeMismatch",
                        "Function should return " + std::string(tyName(want)) +
                        " but this returns " + tyName(valTy) + ".", r->line);
                }
            }
            break;
        }
        case NodeKind::Read: {
            auto* rd = static_cast<Read*>(stmt);
            for (const auto& name : rd->names) {
                const VarEntry* v = findVar(name);
                if (!v) {
                    std::string hint = suggestName(name, knownVarNames());
                    reportTypeError("UndeclaredVariable", "cin reads into '" + name + "', which is not declared." +
                        (hint.empty() ? "" : " Did you mean '" + hint + "'?"), rd->line);
                } else if (v->isConst) {
                    reportTypeError("AssignToConst", "cin cannot read into '" + name + "' because it is const.", rd->line);
                }
            }
            break;
        }
        case NodeKind::FuncDecl: {
            checkFuncBody(static_cast<FuncDecl*>(stmt));
            break;
        }
        case NodeKind::Block: {
            // A nested { ... } block, including the block a for-loop becomes
            // (its initialiser and loop), gets its own scope.
            auto* b = static_cast<Block*>(stmt);
            checkBlock(b, true, b->scopeLabel);
            break;
        }
        case NodeKind::Call: {
            checkCall(static_cast<Call*>(stmt));   // bare call statement
            break;
        }
        default:
            checkExpr(stmt);   // any stray expression
            break;
    }
}

// ---- expression typing ---------------------------------------------------
Ty SemanticAnalyzer::checkExpr(Node* expr) {
    if (!expr) return Ty::Error;
    switch (expr->kind) {
        case NodeKind::IntLiteral:    return Ty::Int;
        case NodeKind::StringLiteral: return Ty::String;
        case NodeKind::BoolLiteral:   return Ty::Bool;
        case NodeKind::Identifier: {
            auto* id = static_cast<Identifier*>(expr);
            Ty t;
            if (lookupVar(id->name, t)) return t;
            std::string hint = suggestName(id->name, knownVarNames());
            std::string msg = "Use of undeclared variable '" + id->name + "'.";
            if (!hint.empty()) msg += " Did you mean '" + hint + "'?";
            reportTypeError("UndeclaredVariable", msg, id->line);
            return Ty::Error;
        }
        case NodeKind::UnaryOp: {
            auto* u = static_cast<UnaryOp*>(expr);
            Ty t = checkExpr(u->operand.get());
            if (t == Ty::Error) return Ty::Error;
            Ty want = u->op == "!" ? Ty::Bool : Ty::Int;     // ! gives bool; unary - gives int
            if (!coerce(u->operand, t, want)) {
                reportTypeError("TypeMismatch",
                    "Operator '" + u->op + "' cannot be applied to a " + std::string(tyName(t)) + ".", u->line);
                return Ty::Error;
            }
            return want;
        }
        case NodeKind::BinaryOp: return checkBinary(static_cast<BinaryOp*>(expr));
        case NodeKind::Call:     return checkCall(static_cast<Call*>(expr));
        case NodeKind::Cast: {                     // an explicit conversion written in the program
            auto* c = static_cast<Cast*>(expr);
            Ty from = checkExpr(c->operand.get());
            Ty to = tyFromName(c->type);
            if (from == Ty::Error) return to;
            bool ok = (to == Ty::String) ? from == Ty::String
                                         : (from == Ty::Int || from == Ty::Bool);
            if (!ok) {
                reportTypeError("TypeMismatch", "Cannot convert " + std::string(tyName(from)) + " to " + c->type + "." +
                    (to == Ty::String ? " Use to_string(n) to turn a number into text." : ""), c->line);
                return Ty::Error;
            }
            return to;
        }
        case NodeKind::Conditional: {
            auto* c = static_cast<Conditional*>(expr);
            Ty ct = checkCondition(c->cond, "?:", c->line);
            Ty a = checkExpr(c->thenExpr.get());
            Ty b = checkExpr(c->elseExpr.get());
            if (ct == Ty::Error || a == Ty::Error || b == Ty::Error) return Ty::Error;
            if (a == b) return a;
            if (coerce(c->thenExpr, a, Ty::Int) && coerce(c->elseExpr, b, Ty::Int)) return Ty::Int;
            reportTypeError("TypeMismatch",
                "Both results of '?:' must have compatible types, not " +
                std::string(tyName(a)) + " and " + tyName(b) + ".", c->line);
            return Ty::Error;
        }
        default:                 return Ty::Error;
    }
}

Ty SemanticAnalyzer::checkBinary(BinaryOp* bin) {
    Ty l = checkExpr(bin->left.get());
    Ty r = checkExpr(bin->right.get());
    if (l == Ty::Error || r == Ty::Error) return Ty::Error;
    const std::string& op = bin->op;
    auto numeric = [](Ty t) { return t == Ty::Int || t == Ty::Bool; };
    auto mismatch = [&](const std::string& hint) {
        reportTypeError("TypeMismatch",
            "Operator '" + op + "' cannot combine " + std::string(tyName(l)) + " and " + tyName(r) + "." + hint,
            bin->line);
        return Ty::Error;
    };

    // Arithmetic and shifts: ints, with bools promoted to int (true is 1), as in C++.
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || op == "<<" || op == ">>") {
        if (op == "+" && l == Ty::String && r == Ty::String) return Ty::String;   // joining strings
        if (numeric(l) && numeric(r)) {
            coerce(bin->left, l, Ty::Int);
            coerce(bin->right, r, Ty::Int);
            return Ty::Int;
        }
        if (op == "+" && (l == Ty::String || r == Ty::String))
            return mismatch(" In C++ this does not join them: adding a number to text moves a pointer into the "
                            "text instead (clang warns: adding 'int' to a string does not append to the string). "
                            "Use to_string(n), or write both with cout << ... << n.");
        return mismatch("");
    }
    // Comparisons: two numbers (bool promoted to int), or two strings (alphabetical order).
    if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=") {
        if (l == Ty::String && r == Ty::String) return Ty::Bool;
        if (numeric(l) && numeric(r)) {
            // Ordering compares numbers, so bools are promoted to int (true < false
            // compares 1 < 0). == and != only need converting when the types differ.
            bool ordering = op == "<" || op == ">" || op == "<=" || op == ">=";
            if (ordering || l != r) { coerce(bin->left, l, Ty::Int); coerce(bin->right, r, Ty::Int); }
            return Ty::Bool;
        }
        return mismatch("");
    }
    // Logic: each side converted to bool (non-zero is true).
    if (op == "&&" || op == "||") {
        if (numeric(l) && numeric(r)) {
            coerce(bin->left, l, Ty::Bool);
            coerce(bin->right, r, Ty::Bool);
            return Ty::Bool;
        }
        return mismatch("");
    }
    return Ty::Error;
}

Ty SemanticAnalyzer::checkCall(Call* call) {
    // Type each argument first (also surfaces nested errors).
    std::vector<Ty> argTypes;
    argTypes.reserve(call->args.size());
    for (auto& a : call->args) argTypes.push_back(checkExpr(a.get()));

    auto it = funcs_.find(call->callee);
    // Built-in to_string(n), from <string>.
    if (it == funcs_.end() && call->callee == "to_string") {
        if (argTypes.size() != 1) {
            reportTypeError("ArgumentCount", "to_string takes exactly one value.", call->line);
            return Ty::Error;
        }
        if (argTypes[0] != Ty::Error && !coerce(call->args[0], argTypes[0], Ty::Int)) {
            reportTypeError("ArgumentType", "to_string needs a number, not a " +
                            std::string(tyName(argTypes[0])) + ".", call->line);
            return Ty::Error;
        }
        return Ty::String;
    }
    if (it == funcs_.end()) {
        std::vector<std::string> names;
        for (const auto& kv : funcs_) names.push_back(kv.first);
        std::string hint = suggestName(call->callee, names);
        std::string msg = "Call to undefined function '" + call->callee + "'.";
        static const char* printLike[] = {"printf", "println", "puts", "printLine", "cout", "echo", "write"};
        bool isPrintLike = false;
        for (const char* p : printLike) if (call->callee == p) isPrintLike = true;
        if (isPrintLike) msg += " To print, use cout, e.g. cout << \"x = \" << x << endl;";
        else if (!hint.empty()) msg += " Did you mean '" + hint + "'?";
        reportTypeError("UndefinedFunction", msg, call->line);
        return Ty::Error;
    }
    const FuncSig& sig = it->second;
    if (argTypes.size() != sig.paramTypes.size()) {
        reportTypeError("ArgumentCount",
            "Function '" + call->callee + "' expects " +
            std::to_string(sig.paramTypes.size()) + " argument(s) but got " +
            std::to_string(argTypes.size()) + ".", call->line);
        return sig.returnType;
    }
    for (size_t i = 0; i < argTypes.size(); ++i) {
        if (argTypes[i] != Ty::Error && !coerce(call->args[i], argTypes[i], sig.paramTypes[i])) {
            reportTypeError("ArgumentType",
                "Argument " + std::to_string(i + 1) + " of '" + call->callee +
                "' should be " + tyName(sig.paramTypes[i]) + " but got " +
                tyName(argTypes[i]) + ".", call->line);
        }
    }
    return sig.returnType;
}

} // namespace lucid
