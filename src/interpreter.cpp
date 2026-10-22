// ============================================================================
// interpreter.cpp — Phase 5 of Lucid.  Tree-walking evaluator.
// Executes the (optimized, already type-checked) AST and collects print output.
// ============================================================================
#include "interpreter.hpp"
#include "intmath.hpp"

namespace lucid {

std::string valueToString(const Value& v) {
    if (std::holds_alternative<int>(v))         return std::to_string(std::get<int>(v));
    if (std::holds_alternative<bool>(v))        return std::get<bool>(v) ? "true" : "false";
    return std::get<std::string>(v);
}

int Interpreter::asInt(const Value& v) { return std::get<int>(v); }
bool Interpreter::asBool(const Value& v) { return std::get<bool>(v); }
const std::string& Interpreter::asStr(const Value& v) { return std::get<std::string>(v); }

Interpreter::Interpreter(const std::string& source, ErrorList& errors)
    : source_(source), errors_(errors) {}

void Interpreter::define(const std::string& name, const Value& v) {
    scopes_.back()[name] = v;
}

bool Interpreter::assign(const std::string& name, const Value& v) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) { found->second = v; return true; }
    }
    return false;
}

bool Interpreter::lookup(const std::string& name, Value& out) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) { out = found->second; return true; }
    }
    return false;
}

void Interpreter::spendStep(int line) {
    if (++steps_ > kMaxSteps)
        throw RuntimeFault{ "Stopped after 2,000,000 steps. Is there an infinite loop, "
                            "or a loop variable that never changes?", line };
}

std::vector<std::string> Interpreter::run(Program& program) {
    output_.clear();
    outputBytes_ = 0;
    steps_ = 0;
    callDepth_ = 0;
    scopes_.clear();
    scopes_.emplace_back();   // global scope

    // Register all top-level functions first (forward references allowed).
    for (auto& stmt : program.statements)
        if (stmt->kind == NodeKind::FuncDecl)
            funcs_[static_cast<FuncDecl*>(stmt.get())->name] = static_cast<FuncDecl*>(stmt.get());

    try {
        for (auto& stmt : program.statements) {
            if (stmt->kind == NodeKind::FuncDecl) continue;  // definitions, not executed here
            execStatement(stmt.get());
        }
        if (runsMainAutomatically(program)) {   // C-style program: run main()
            Call mainCall(funcs_["main"]->line);
            mainCall.callee = "main";
            callFunction(&mainCall);
        }
    } catch (const RuntimeFault& f) {
        errors_.add("runtime", "RuntimeError", f.message, f.line, 0, sourceLine(source_, f.line));
    } catch (const std::bad_variant_access&) {
        // Unreachable for type-checked programs; a safety net so a checker gap
        // can never crash the compiler or the playground server.
        errors_.add("runtime", "InternalError",
                    "A value had an unexpected type while running. Please report this program.", 0, 0, "");
    } catch (const ReturnSignal&) {
        // A stray top-level return — semantic analysis already flagged it.
    }
    return output_;
}

void Interpreter::execBlock(Block* block) {
    scopes_.emplace_back();
    struct Pop { std::vector<Scope>& s; ~Pop(){ s.pop_back(); } } pop{scopes_};
    for (auto& s : block->statements) execStatement(s.get());
}

void Interpreter::execStatement(Node* stmt) {
    switch (stmt->kind) {
        case NodeKind::VarDecl: {
            auto* d = static_cast<VarDecl*>(stmt);
            define(d->name, evalExpr(d->init.get()));
            break;
        }
        case NodeKind::Assign: {
            auto* a = static_cast<Assign*>(stmt);
            assign(a->name, evalExpr(a->value.get()));
            break;
        }
        case NodeKind::If: {
            auto* i = static_cast<If*>(stmt);
            if (asBool(evalExpr(i->cond.get())))
                execBlock(i->thenBlock.get());
            else if (i->elseBlock)
                execBlock(i->elseBlock.get());
            break;
        }
        case NodeKind::While: {
            auto* w = static_cast<While*>(stmt);
            // A do-while runs its body once before the first test.
            for (bool first = true; ; first = false) {
                if (!(w->doWhile && first) && !asBool(evalExpr(w->cond.get()))) break;
                spendStep(w->line);
                try {
                    execBlock(w->body.get());
                } catch (const BreakSignal&) {
                    break;
                } catch (const ContinueSignal&) {
                    // fall through to the update, then the next test
                }
                if (w->step) execStatement(w->step.get());   // for-loop update
            }
            break;
        }
        case NodeKind::Break:    throw BreakSignal{};
        case NodeKind::Continue: throw ContinueSignal{};
        case NodeKind::Print: {
            auto* p = static_cast<Print*>(stmt);
            if (output_.size() >= kMaxOutputLines)
                throw RuntimeFault{ "Output limit reached (10,000 lines). Is a loop printing forever?", p->line };
            std::string line;   // print(a, b, c): one line, values separated by spaces
            for (size_t i = 0; i < p->args.size(); ++i) {
                if (i) line += ' ';
                line += valueToString(evalExpr(p->args[i].get()));
            }
            outputBytes_ += line.size() + 1;
            if (outputBytes_ > kMaxOutputBytes)
                throw RuntimeFault{ "Output limit reached (2 MB of text). Is a loop printing forever?", p->line };
            output_.push_back(line);
            break;
        }
        case NodeKind::Return: {
            auto* r = static_cast<Return*>(stmt);
            throw ReturnSignal{ r->value ? evalExpr(r->value.get()) : Value{0} };   // `return;`: no value
        }
        case NodeKind::Block:
            // The optimizer may replace an if(true){...} with its bare block.
            execBlock(static_cast<Block*>(stmt));
            break;
        case NodeKind::FuncDecl:
            break;   // nested definitions: skip (registered at top level)
        case NodeKind::Call:
            evalExpr(stmt);   // bare call statement, discard result
            break;
        default:
            evalExpr(stmt);
            break;
    }
}

Value Interpreter::evalExpr(Node* expr) {
    switch (expr->kind) {
        case NodeKind::IntLiteral:    return Value{ static_cast<IntLiteral*>(expr)->value };
        case NodeKind::StringLiteral: return Value{ static_cast<StringLiteral*>(expr)->value };
        case NodeKind::BoolLiteral:   return Value{ static_cast<BoolLiteral*>(expr)->value };
        case NodeKind::Identifier: {
            auto* id = static_cast<Identifier*>(expr);
            Value v;
            if (lookup(id->name, v)) return v;
            throw RuntimeFault{ "Undeclared variable '" + id->name + "' at runtime.", id->line };
        }
        case NodeKind::UnaryOp: {
            auto* u = static_cast<UnaryOp*>(expr);
            Value operand = evalExpr(u->operand.get());
            if (u->op == "!") return Value{ !asBool(operand) };
            int neg;                            // unary '-'
            if (!checkedNeg(asInt(operand), neg))
                throw RuntimeFault{ "Integer overflow: -(-2147483648) does not fit in an int.", u->line };
            return Value{ neg };
        }
        case NodeKind::BinaryOp: return evalBinary(static_cast<BinaryOp*>(expr));
        case NodeKind::Call:     return callFunction(static_cast<Call*>(expr));
        case NodeKind::Conditional: {
            auto* c = static_cast<Conditional*>(expr);
            return asBool(evalExpr(c->cond.get())) ? evalExpr(c->thenExpr.get())
                                                   : evalExpr(c->elseExpr.get());
        }
        default:
            throw RuntimeFault{ "Cannot evaluate expression.", expr->line };
    }
}

Value Interpreter::evalBinary(BinaryOp* bin) {
    const std::string& op = bin->op;

    // Short-circuit boolean operators.
    if (op == "&&") {
        if (!asBool(evalExpr(bin->left.get()))) return Value{ false };
        return Value{ asBool(evalExpr(bin->right.get())) };
    }
    if (op == "||") {
        if (asBool(evalExpr(bin->left.get()))) return Value{ true };
        return Value{ asBool(evalExpr(bin->right.get())) };
    }

    Value l = evalExpr(bin->left.get());
    Value r = evalExpr(bin->right.get());

    if (op == "+") {
        if (std::holds_alternative<std::string>(l) || std::holds_alternative<std::string>(r)) {
            std::string a = valueToString(l), b = valueToString(r);   // "n = " + 5
            if (a.size() + b.size() > kMaxStringLength)
                throw RuntimeFault{ "String too long: over 1,000,000 characters. "
                                    "Is a loop joining text forever?", bin->line };
            return Value{ a + b };
        }
    }

    // Integer arithmetic, checked: overflow is a runtime error, never
    // undefined behaviour (see intmath.hpp).
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        int a = asInt(l), b = asInt(r), out = 0;
        bool ok = true;
        if (op == "+") ok = checkedAdd(a, b, out);
        else if (op == "-") ok = checkedSub(a, b, out);
        else if (op == "*") ok = checkedMul(a, b, out);
        else {
            if (b == 0) throw RuntimeFault{ op == "/" ? "Division by zero." : "Modulo by zero.", bin->line };
            if (op == "/") ok = checkedDiv(a, b, out);
            else out = safeMod(a, b);
        }
        if (!ok)
            throw RuntimeFault{ "Integer overflow: " + std::to_string(a) + " " + op + " " +
                                std::to_string(b) + " does not fit in an int "
                                "(range -2147483648 to 2147483647).", bin->line };
        return Value{ out };
    }
    if (op == "<")  return Value{ asInt(l) <  asInt(r) };
    if (op == ">")  return Value{ asInt(l) >  asInt(r) };
    if (op == "<=") return Value{ asInt(l) <= asInt(r) };
    if (op == ">=") return Value{ asInt(l) >= asInt(r) };
    if (op == "==") return Value{ l == r };
    if (op == "!=") return Value{ l != r };

    throw RuntimeFault{ "Unknown operator '" + op + "'.", bin->line };
}

Value Interpreter::callFunction(Call* call) {
    auto it = funcs_.find(call->callee);
    if (it == funcs_.end())
        throw RuntimeFault{ "Call to undefined function '" + call->callee + "'.", call->line };
    FuncDecl* fn = it->second;

    // Evaluate arguments in the *caller's* scope first.
    std::vector<Value> args;
    args.reserve(call->args.size());
    for (auto& a : call->args) args.push_back(evalExpr(a.get()));

    spendStep(call->line);
    if (++callDepth_ > kMaxCallDepth) {
        --callDepth_;
        throw RuntimeFault{ "Call stack overflow: more than 10,000 nested calls (runaway recursion?).", call->line };
    }

    // Functions run in an isolated scope chain: only params are visible.
    auto savedScopes = std::move(scopes_);
    scopes_.clear();
    scopes_.emplace_back();
    for (size_t i = 0; i < fn->params.size() && i < args.size(); ++i)
        define(fn->params[i].name, args[i]);

    Value result;
    bool returned = false;
    try {
        for (auto& s : fn->body->statements) execStatement(s.get());
    } catch (const ReturnSignal& rs) {
        result = rs.value;
        returned = true;
    }

    scopes_ = std::move(savedScopes);
    --callDepth_;

    if (returned) return result;

    // Fell off the end without returning: hand back a type-appropriate default.
    if (fn->returnType == "int")    return Value{ 0 };
    if (fn->returnType == "bool")   return Value{ false };
    return Value{ std::string("") };
}

} // namespace lucid
