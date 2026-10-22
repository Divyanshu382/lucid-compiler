// ============================================================================
// ast.hpp — Abstract Syntax Tree node classes for Lucid.
// Phase: (shared)  Input: built by the Parser.  Output: consumed by the
// SemanticAnalyzer, Optimizer, and Interpreter; serialized via toJson() for
// the playground's graphical tree.
//
// Ownership: every child is a std::unique_ptr<Node> (no raw new/delete).
// Each toJson() emits a uniform shape { kind, label, line, children:[...] }
// so the front-end can draw any node the same way.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../third_party/json.hpp"

namespace lucid {

enum class NodeKind {
    Program, Block,
    VarDecl, Assign, If, While, Print, FuncDecl, Return,
    Break, Continue,
    Call, BinaryOp, UnaryOp, Conditional,
    Identifier, IntLiteral, StringLiteral, BoolLiteral
};

inline const char* nodeKindName(NodeKind k) {
    switch (k) {
        case NodeKind::Program:       return "Program";
        case NodeKind::Block:         return "Block";
        case NodeKind::VarDecl:       return "VarDecl";
        case NodeKind::Assign:        return "Assign";
        case NodeKind::If:            return "If";
        case NodeKind::While:         return "While";
        case NodeKind::Print:         return "Print";
        case NodeKind::FuncDecl:      return "FuncDecl";
        case NodeKind::Return:        return "Return";
        case NodeKind::Break:         return "Break";
        case NodeKind::Continue:      return "Continue";
        case NodeKind::Conditional:   return "Conditional";
        case NodeKind::Call:          return "Call";
        case NodeKind::BinaryOp:      return "BinaryOp";
        case NodeKind::UnaryOp:       return "UnaryOp";
        case NodeKind::Identifier:    return "Identifier";
        case NodeKind::IntLiteral:    return "IntLiteral";
        case NodeKind::StringLiteral: return "StringLiteral";
        case NodeKind::BoolLiteral:   return "BoolLiteral";
    }
    return "Unknown";
}

// ---- Base ----------------------------------------------------------------
struct Node {
    NodeKind kind;
    int line;
    Node(NodeKind k, int ln) : kind(k), line(ln) {}
    virtual ~Node() = default;
    virtual nlohmann::json toJson() const = 0;
};
using NodePtr = std::unique_ptr<Node>;

// Helper: base JSON envelope every node shares.
inline nlohmann::json nodeBase(NodeKind k, const std::string& label, int line) {
    return nlohmann::json{
        {"kind", nodeKindName(k)},
        {"label", label},
        {"line", line},
        {"children", nlohmann::json::array()}
    };
}

// A typed function parameter, e.g. `int a`.
struct Param {
    std::string type;   // "int" | "string" | "bool"
    std::string name;
};

// ---- Container nodes -----------------------------------------------------
struct Program : Node {
    std::vector<NodePtr> statements;
    Program() : Node(NodeKind::Program, 0) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Program", line);
        for (const auto& s : statements) j["children"].push_back(s->toJson());
        return j;
    }
};

struct Block : Node {
    std::vector<NodePtr> statements;
    std::string scopeLabel = "block";   // shown in the symbol table ("for", "block", ...)
    explicit Block(int ln) : Node(NodeKind::Block, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Block", line);
        for (const auto& s : statements) j["children"].push_back(s->toJson());
        return j;
    }
};

// ---- Statements ----------------------------------------------------------
struct VarDecl : Node {
    std::string declType;   // "int" | "string" | "bool"
    std::string name;
    NodePtr init;           // required initializer
    explicit VarDecl(int ln) : Node(NodeKind::VarDecl, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "VarDecl: " + declType + " " + name, line);
        if (init) j["children"].push_back(init->toJson());
        return j;
    }
};

struct Assign : Node {
    std::string name;
    NodePtr value;
    explicit Assign(int ln) : Node(NodeKind::Assign, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Assign: " + name, line);
        if (value) j["children"].push_back(value->toJson());
        return j;
    }
};

struct If : Node {
    NodePtr cond;
    std::unique_ptr<Block> thenBlock;
    std::unique_ptr<Block> elseBlock;   // may be null
    explicit If(int ln) : Node(NodeKind::If, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "If", line);
        if (cond)      j["children"].push_back(cond->toJson());
        if (thenBlock) { auto t = thenBlock->toJson(); t["label"] = "then"; j["children"].push_back(std::move(t)); }
        if (elseBlock) { auto e = elseBlock->toJson(); e["label"] = "else"; j["children"].push_back(std::move(e)); }
        return j;
    }
};

// Also represents `do { } while (c);` (doWhile = true: the body runs before
// the first test) and `for` loops, whose update part is kept in `step` so that
// `continue` still runs it before the next test.
struct While : Node {
    NodePtr cond;
    std::unique_ptr<Block> body;
    NodePtr step;            // for-loop update (e.g. i = i + 1); may be null
    bool doWhile = false;
    explicit While(int ln) : Node(NodeKind::While, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, doWhile ? "DoWhile" : "While", line);
        if (cond) j["children"].push_back(cond->toJson());
        if (body) { auto b = body->toJson(); b["label"] = "body"; j["children"].push_back(std::move(b)); }
        if (step) j["children"].push_back(step->toJson());
        return j;
    }
};

// print(a, b, ...) prints its values on one line, separated by spaces.
struct Print : Node {
    std::vector<NodePtr> args;
    explicit Print(int ln) : Node(NodeKind::Print, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Print", line);
        for (const auto& a : args) j["children"].push_back(a->toJson());
        return j;
    }
};

struct FuncDecl : Node {
    std::string name;
    std::vector<Param> params;
    std::string returnType;
    std::unique_ptr<Block> body;
    explicit FuncDecl(int ln) : Node(NodeKind::FuncDecl, ln) {}
    std::string signature() const {
        std::string s = name + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i) s += ", ";
            s += params[i].type + " " + params[i].name;
        }
        s += "): " + returnType;
        return s;
    }
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "func " + signature(), line);
        if (body) { auto b = body->toJson(); b["label"] = "body"; j["children"].push_back(std::move(b)); }
        return j;
    }
};

struct Return : Node {
    NodePtr value;   // null for a bare `return;` (void functions)
    explicit Return(int ln) : Node(NodeKind::Return, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Return", line);
        if (value) j["children"].push_back(value->toJson());
        return j;
    }
};

struct Break : Node {
    explicit Break(int ln) : Node(NodeKind::Break, ln) {}
    nlohmann::json toJson() const override { return nodeBase(kind, "Break", line); }
};

struct Continue : Node {
    explicit Continue(int ln) : Node(NodeKind::Continue, ln) {}
    nlohmann::json toJson() const override { return nodeBase(kind, "Continue", line); }
};

// ---- Expressions ---------------------------------------------------------
// cond ? thenExpr : elseExpr
struct Conditional : Node {
    NodePtr cond, thenExpr, elseExpr;
    explicit Conditional(int ln) : Node(NodeKind::Conditional, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "?:", line);
        if (cond)     j["children"].push_back(cond->toJson());
        if (thenExpr) j["children"].push_back(thenExpr->toJson());
        if (elseExpr) j["children"].push_back(elseExpr->toJson());
        return j;
    }
};

struct Call : Node {
    std::string callee;
    std::vector<NodePtr> args;
    explicit Call(int ln) : Node(NodeKind::Call, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, "Call: " + callee, line);
        for (const auto& a : args) j["children"].push_back(a->toJson());
        return j;
    }
};

struct BinaryOp : Node {
    std::string op;         // + - * / % < > <= >= == != && ||
    NodePtr left, right;
    explicit BinaryOp(int ln) : Node(NodeKind::BinaryOp, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, op, line);
        if (left)  j["children"].push_back(left->toJson());
        if (right) j["children"].push_back(right->toJson());
        return j;
    }
};

struct UnaryOp : Node {
    std::string op;         // ! or -
    NodePtr operand;
    explicit UnaryOp(int ln) : Node(NodeKind::UnaryOp, ln) {}
    nlohmann::json toJson() const override {
        auto j = nodeBase(kind, op + " (unary)", line);
        if (operand) j["children"].push_back(operand->toJson());
        return j;
    }
};

struct Identifier : Node {
    std::string name;
    explicit Identifier(int ln) : Node(NodeKind::Identifier, ln) {}
    nlohmann::json toJson() const override {
        return nodeBase(kind, name, line);
    }
};

struct IntLiteral : Node {
    int value = 0;
    explicit IntLiteral(int ln) : Node(NodeKind::IntLiteral, ln) {}
    nlohmann::json toJson() const override {
        return nodeBase(kind, std::to_string(value), line);
    }
};

struct StringLiteral : Node {
    std::string value;
    explicit StringLiteral(int ln) : Node(NodeKind::StringLiteral, ln) {}
    nlohmann::json toJson() const override {
        return nodeBase(kind, "\"" + value + "\"", line);
    }
};

struct BoolLiteral : Node {
    bool value = false;
    explicit BoolLiteral(int ln) : Node(NodeKind::BoolLiteral, ln) {}
    nlohmann::json toJson() const override {
        return nodeBase(kind, value ? "true" : "false", line);
    }
};

// A C-style program puts its code in main(). If the program declares a
// parameterless main() and has no top-level code other than declarations,
// main() is run automatically, as in C. Shared by the interpreter and the
// intermediate-code generator so both agree.
inline bool runsMainAutomatically(const Program& program) {
    bool hasMain = false;
    for (const auto& s : program.statements) {
        if (s->kind == NodeKind::FuncDecl) {
            auto* f = static_cast<const FuncDecl*>(s.get());
            if (f->name == "main" && f->params.empty()) hasMain = true;
        } else if (s->kind != NodeKind::VarDecl) {
            return false;   // the program has its own top-level code
        }
    }
    return hasMain;
}

// The children of any node, for code that walks the tree without recursion.
inline std::vector<const Node*> childrenOf(const Node& n) {
    std::vector<const Node*> out;
    auto add = [&](const Node* c) { if (c) out.push_back(c); };
    switch (n.kind) {
        case NodeKind::Program:  for (auto& s : static_cast<const Program&>(n).statements) add(s.get()); break;
        case NodeKind::Block:    for (auto& s : static_cast<const Block&>(n).statements) add(s.get()); break;
        case NodeKind::VarDecl:  add(static_cast<const VarDecl&>(n).init.get()); break;
        case NodeKind::Assign:   add(static_cast<const Assign&>(n).value.get()); break;
        case NodeKind::If: {
            auto& i = static_cast<const If&>(n);
            add(i.cond.get()); add(i.thenBlock.get()); add(i.elseBlock.get()); break;
        }
        case NodeKind::While: {
            auto& w = static_cast<const While&>(n);
            add(w.cond.get()); add(w.body.get()); add(w.step.get()); break;
        }
        case NodeKind::Print:    for (auto& a : static_cast<const Print&>(n).args) add(a.get()); break;
        case NodeKind::FuncDecl: add(static_cast<const FuncDecl&>(n).body.get()); break;
        case NodeKind::Return:   add(static_cast<const Return&>(n).value.get()); break;
        case NodeKind::Call:     for (auto& a : static_cast<const Call&>(n).args) add(a.get()); break;
        case NodeKind::BinaryOp: {
            auto& b = static_cast<const BinaryOp&>(n);
            add(b.left.get()); add(b.right.get()); break;
        }
        case NodeKind::UnaryOp:  add(static_cast<const UnaryOp&>(n).operand.get()); break;
        case NodeKind::Conditional: {
            auto& c = static_cast<const Conditional&>(n);
            add(c.cond.get()); add(c.thenExpr.get()); add(c.elseExpr.get()); break;
        }
        default: break;
    }
    return out;
}

// Depth of the tree, computed with an explicit stack so it is safe on any
// input. Returns the depth and, through `deepest`, the line of a deepest node.
inline int treeDepth(const Node& root, int& deepestLine) {
    int best = 0;
    deepestLine = root.line;
    std::vector<std::pair<const Node*, int>> work{{&root, 1}};
    while (!work.empty()) {
        auto [node, depth] = work.back();
        work.pop_back();
        if (depth > best) { best = depth; deepestLine = node->line; }
        for (const Node* c : childrenOf(*node)) work.push_back({c, depth + 1});
    }
    return best;
}

// Count every node in a serialized AST (containers included). Used by the
// optimizer to report node counts before/after.
inline int countJsonNodes(const nlohmann::json& j) {
    int n = 1;
    if (j.contains("children"))
        for (const auto& c : j["children"]) n += countJsonNodes(c);
    return n;
}

} // namespace lucid
