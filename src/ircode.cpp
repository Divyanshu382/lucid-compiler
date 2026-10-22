// ============================================================================
// ircode.cpp — Intermediate code generation (three-address code).
// See ircode.hpp for the quadruple format.
// ============================================================================
#include "ircode.hpp"

namespace lucid {

std::string Quad::text() const {
    if (op == "=")       return result + " = " + arg1;
    if (op == "neg")     return result + " = -" + arg1;
    if (op == "!")       return result + " = !" + arg1;
    if (op == "label")   return result + ":";
    if (op == "goto")    return "goto " + result;
    if (op == "iffalse") return "iffalse " + arg1 + " goto " + result;
    if (op == "iftrue")  return "iftrue " + arg1 + " goto " + result;
    if (op == "param")   return "param " + arg1;
    if (op == "recv")    return "recv " + result;
    if (op == "call")    return result + " = call " + arg1 + ", " + arg2;
    if (op == "ret")     return arg1.empty() ? "return" : "return " + arg1;
    if (op == "print")   return "print " + arg1;
    if (op == "out")     return "cout << " + arg1;
    if (op == "read")    return "cin >> " + result;
    if (op == "cast")    return result + " = (" + arg2 + ") " + arg1;
    if (op == "func")    return "func " + arg1 + ":";
    if (op == "endfunc") return "endfunc " + arg1;
    return result + " = " + arg1 + " " + op + " " + arg2;   // binary operator
}

std::vector<Quad> IRGenerator::generate(const Program& program) {
    code_.clear();
    loops_.clear();
    scopes_.assign(1, {});        // global scope
    renames_.clear();
    temps_ = labels_ = 0;
    for (const auto& s : program.statements) genStmt(*s);
    if (runsMainAutomatically(program)) {     // C-style program: main() is called last
        emit("call", "main", "0", newTemp());
    }
    return code_;
}

void IRGenerator::genBlock(const Block& b) {
    scopes_.emplace_back();
    for (const auto& s : b.statements) genStmt(*s);
    scopes_.pop_back();
}

// A source variable spelled like a compiler temporary (t0, t1, ...) would
// share storage with it, so it is renamed too.
static bool looksLikeTemp(const std::string& name) {
    if (name.size() < 2 || name[0] != 't') return false;
    for (size_t i = 1; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

std::string IRGenerator::declareName(const std::string& name) {
    std::string irName = name;
    if (!resolveName(name).empty() || looksLikeTemp(name))   // hides an outer variable, or clashes with a temporary
        irName = name + "." + std::to_string(++renames_[name]);
    scopes_.back()[name] = irName;
    return irName;
}

std::string IRGenerator::resolveName(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return "";
}

void IRGenerator::genStmt(const Node& n) {
    switch (n.kind) {
        case NodeKind::Block:
            genBlock(static_cast<const Block&>(n));
            break;

        case NodeKind::VarDecl: {
            const auto& d = static_cast<const VarDecl&>(n);
            std::string value = genExpr(*d.init);    // the initialiser sees the outer variable
            emit("=", value, "", declareName(d.name));
            break;
        }
        case NodeKind::Assign: {
            const auto& a = static_cast<const Assign&>(n);
            std::string value = genExpr(*a.value);
            std::string target = resolveName(a.name);
            emit("=", value, "", target.empty() ? a.name : target);
            break;
        }
        case NodeKind::Print: {
            const auto& p = static_cast<const Print&>(n);
            if (p.stream) {                          // cout << a << b: one output per value
                for (const auto& a : p.args) emit("out", genExpr(*a));
                break;
            }
            if (p.args.size() == 1) {
                emit("print", genExpr(*p.args[0]));
            } else {                     // print(a, b): built-in call with n params
                std::vector<std::string> vals;
                for (const auto& a : p.args) vals.push_back(genExpr(*a));
                for (const auto& v : vals) emit("param", v);
                emit("call", "print", std::to_string(vals.size()), newTemp());
            }
            break;
        }

        case NodeKind::Return: {
            const auto& r = static_cast<const Return&>(n);
            emit("ret", r.value ? genExpr(*r.value) : "");
            break;
        }

        case NodeKind::Read: {
            for (const auto& name : static_cast<const Read&>(n).names) {
                std::string irName = resolveName(name);
                emit("read", "", "", irName.empty() ? name : irName);
            }
            break;
        }

        case NodeKind::Break:
            if (!loops_.empty()) emit("goto", "", "", loops_.back().breakTo);
            break;
        case NodeKind::Continue:
            if (!loops_.empty()) emit("goto", "", "", loops_.back().continueTo);
            break;

        // if (c) A else B   ==>   iffalse c goto Lelse ; A ; goto Lend ;
        //                          Lelse: B ; Lend:
        case NodeKind::If: {
            const auto& i = static_cast<const If&>(n);
            std::string c = genExpr(*i.cond);
            std::string lElse = newLabel();
            emit("iffalse", c, "", lElse);
            genBlock(*i.thenBlock);
            if (i.elseBlock) {
                std::string lEnd = newLabel();
                emit("goto", "", "", lEnd);
                emit("label", "", "", lElse);
                genBlock(*i.elseBlock);
                emit("label", "", "", lEnd);
            } else {
                emit("label", "", "", lElse);
            }
            break;
        }

        // while (c) B      ==>  Lstart: iffalse c goto Lend ; B ; [Lstep: step] ; goto Lstart ; Lend:
        // do B while (c);  ==>  Lstart: B ; Lcond: iffalse c goto Lend ; goto Lstart ; Lend:
        case NodeKind::While: {
            const auto& w = static_cast<const While&>(n);
            std::string lStart = newLabel();
            std::string lEnd = newLabel();
            if (w.doWhile) {
                std::string lCond = newLabel();
                emit("label", "", "", lStart);
                loops_.push_back({lCond, lEnd});
                genBlock(*w.body);
                loops_.pop_back();
                emit("label", "", "", lCond);
                std::string c = genExpr(*w.cond);
                emit("iffalse", c, "", lEnd);
                emit("goto", "", "", lStart);
                emit("label", "", "", lEnd);
                break;
            }
            std::string lStep = w.step ? newLabel() : lStart;   // where `continue` goes
            emit("label", "", "", lStart);
            std::string c = genExpr(*w.cond);
            emit("iffalse", c, "", lEnd);
            loops_.push_back({lStep, lEnd});
            genBlock(*w.body);
            loops_.pop_back();
            if (w.step) {
                emit("label", "", "", lStep);
                genStmt(*w.step);
            }
            emit("goto", "", "", lStart);
            emit("label", "", "", lEnd);
            break;
        }

        case NodeKind::FuncDecl: {
            const auto& f = static_cast<const FuncDecl&>(n);
            emit("func", f.name);
            scopes_.emplace_back();                     // parameters; globals stay visible
            for (const auto& p : f.params) emit("recv", "", "", declareName(p.name));
            genBlock(*f.body);
            scopes_.pop_back();
            emit("endfunc", f.name);
            break;
        }

        // A call used as a statement: evaluate it, discard the result.
        case NodeKind::Call:
            genExpr(n);
            break;

        default:
            break;
    }
}

std::string IRGenerator::genExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::IntLiteral:
            return std::to_string(static_cast<const IntLiteral&>(n).value);
        case NodeKind::StringLiteral: {
            std::string out = "\"";
            for (char c : static_cast<const StringLiteral&>(n).value) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            return out + "\"";
        }
        case NodeKind::BoolLiteral:
            return static_cast<const BoolLiteral&>(n).value ? "true" : "false";
        case NodeKind::Identifier: {
            const std::string& name = static_cast<const Identifier&>(n).name;
            std::string irName = resolveName(name);
            return irName.empty() ? name : irName;
        }

        case NodeKind::Cast: {
            const auto& c = static_cast<const Cast&>(n);
            std::string a = genExpr(*c.operand);
            std::string t = newTemp();
            emit("cast", a, c.type, t);
            return t;
        }

        // Short-circuit code: the right operand of && / || is only evaluated
        // when the left one does not already decide the result.
        //   t = a && b  ==>  t = a ; iffalse t goto Lend ; t = b ; Lend:
        //   t = a || b  ==>  t = a ; iftrue  t goto Lend ; t = b ; Lend:
        case NodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOp&>(n);
            if (b.op == "&&" || b.op == "||") {
                std::string t = newTemp();
                std::string lEnd = newLabel();
                emit("=", genExpr(*b.left), "", t);
                emit(b.op == "&&" ? "iffalse" : "iftrue", t, "", lEnd);
                emit("=", genExpr(*b.right), "", t);
                emit("label", "", "", lEnd);
                return t;
            }
            std::string l = genExpr(*b.left);
            std::string r = genExpr(*b.right);
            std::string t = newTemp();
            emit(b.op, l, r, t);
            return t;
        }
        case NodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOp&>(n);
            std::string a = genExpr(*u.operand);
            std::string t = newTemp();
            emit(u.op == "-" ? "neg" : "!", a, "", t);
            return t;
        }
        // t = c ? a : b  ==>  iffalse c goto Lelse ; t = a ; goto Lend ; Lelse: t = b ; Lend:
        case NodeKind::Conditional: {
            const auto& c = static_cast<const Conditional&>(n);
            std::string cond = genExpr(*c.cond);
            std::string t = newTemp();
            std::string lElse = newLabel(), lEnd = newLabel();
            emit("iffalse", cond, "", lElse);
            emit("=", genExpr(*c.thenExpr), "", t);
            emit("goto", "", "", lEnd);
            emit("label", "", "", lElse);
            emit("=", genExpr(*c.elseExpr), "", t);
            emit("label", "", "", lEnd);
            return t;
        }
        case NodeKind::Call: {
            const auto& c = static_cast<const Call&>(n);
            std::vector<std::string> args;
            for (const auto& a : c.args) args.push_back(genExpr(*a));
            for (const auto& a : args) emit("param", a);   // params after all are evaluated
            std::string t = newTemp();
            emit("call", c.callee, std::to_string(args.size()), t);
            return t;
        }
        default:
            return "?";
    }
}

nlohmann::json quadsToJson(const std::vector<Quad>& code) {
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < code.size(); ++i) {
        const auto& q = code[i];
        arr.push_back({{"n", i}, {"op", q.op}, {"arg1", q.arg1},
                       {"arg2", q.arg2}, {"result", q.result}, {"text", q.text()}});
    }
    return arr;
}

} // namespace lucid
