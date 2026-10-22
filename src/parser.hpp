// ============================================================================
// parser.hpp — Phase 2 of Lucid.
// Input : std::vector<Token>
// Output: std::unique_ptr<Program> AST  (+ syntax errors into an ErrorList)
//
// A hand-written recursive-descent parser. On a syntax error it records the
// diagnostic, then panic-mode-synchronizes to the next statement so more than
// one error can be reported per run.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "token.hpp"
#include "ast.hpp"
#include "error.hpp"

namespace lucid {

class Parser {
public:
    Parser(std::vector<Token> tokens, const std::string& source, ErrorList& errors);
    std::unique_ptr<Program> parse();

private:
    std::vector<Token> tokens_;
    const std::string& source_;
    ErrorList& errors_;
    size_t pos_ = 0;

    // `int a = 1, b, c = 3;` yields one VarDecl per name. The first is returned
    // as the statement; the rest wait here and are appended right after it by
    // whichever statement list is being parsed.
    std::vector<NodePtr> pending_;
    void drainPending(std::vector<NodePtr>& into);

    // Nesting limit, like a C++ compiler's (clang stops at 256 nested
    // brackets): parentheses, blocks and unary operators each add a level.
    // Past the limit the parser reports one error and stops, so absurd input
    // can never exhaust the stack.
    static constexpr int kMaxNesting = 256;
    int nesting_ = 0;
    bool tooDeep_ = false;
    void enterNesting();
    struct Nest {
        Parser& p;
        explicit Nest(Parser& parser) : p(parser) { p.enterNesting(); }
        ~Nest() { --p.nesting_; }
    };

    // Thrown internally to unwind to the nearest recovery point.
    struct ParseError {};

    // --- token cursor helpers ---
    const Token& peek() const { return tokens_[pos_]; }
    const Token& previous() const { return tokens_[pos_ - 1]; }
    bool isAtEnd() const { return peek().type == TokenType::EndOfFile; }
    bool check(TokenType t) const { return peek().type == t; }
    const Token& advance();
    bool match(TokenType t);
    const Token& expect(TokenType t, const std::string& what);
    [[noreturn]] void error(const Token& tok, const std::string& message);
    void synchronize();

    // --- grammar ---
    NodePtr topLevel();
    NodePtr statement();
    std::unique_ptr<Block> block();
    NodePtr varDecl();
    NodePtr simpleStatement();  // assignment or bare call, starting with an identifier
    NodePtr ifStatement();
    NodePtr doWhileStatement();
    NodePtr coutStatement();             // cout << a << b << endl;
    NodePtr cinStatement();              // cin >> a >> b;
    NodePtr skipUsing();                 // using namespace std;  (nothing to import)
    std::unique_ptr<Block> bodyBlock();  // { ... } or a single statement, as in C++
    NodePtr breakOrContinue();
    NodePtr cFuncDecl();                 // C-style: int add(int a, int b) { ... }
    NodePtr skipNestedFunction();        // report a function inside a block, skip it whole
    bool isPrototype() const;            // int add(int, int);  (no body)
    void parseParams(FuncDecl& fn);      // ( type name, ... )  or  (void)
    std::string returnTypeName();        // a type, or void
    bool isCFuncStart() const;           // type/void IDENT '(' ...
    NodePtr declarator(const std::string& type, bool isConst = false);
    static NodePtr defaultValue(const std::string& type, int line);
    NodePtr forStatement();
    NodePtr assignmentNoSemi();   // `x = e` / `x += e` without the trailing ';'
    bool isCompoundAssign(TokenType t) const;
    bool isTypeStart() const;
    NodePtr whileStatement();
    NodePtr printStatement();
    NodePtr returnStatement();
    NodePtr funcDecl();
    std::string typeName();

    // --- expressions (precedence climbing) ---
    NodePtr expression();
    NodePtr conditional();               // c ? a : b
    NodePtr logicalOr();
    NodePtr logicalAnd();
    NodePtr equality();
    NodePtr comparison();
    NodePtr shift();
    NodePtr term();
    NodePtr factor();
    NodePtr unary();
    NodePtr primary();
    NodePtr finishCall(const Token& name);
};

} // namespace lucid
