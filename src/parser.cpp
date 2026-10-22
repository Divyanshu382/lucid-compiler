// ============================================================================
// parser.cpp — Phase 2 of Lucid.  tokens -> AST.
// Recursive descent with precedence-climbing expressions and panic-mode
// recovery. Every node records the source line it came from.
// ============================================================================
#include "parser.hpp"

#include <unordered_map>

namespace lucid {

Parser::Parser(std::vector<Token> tokens, const std::string& source, ErrorList& errors)
    : tokens_(std::move(tokens)), source_(source), errors_(errors) {}

const Token& Parser::advance() {
    if (!isAtEnd()) ++pos_;
    return previous();
}

bool Parser::match(TokenType t) {
    if (check(t)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenType t, const std::string& what) {
    if (check(t)) return advance();
    error(peek(), "expected " + what + " but found '" +
                  (peek().type == TokenType::EndOfFile ? "end of file" : peek().value) + "'");
}

void Parser::error(const Token& tok, const std::string& message) {
    errors_.add("syntax", "UnexpectedToken", message, tok.line, tok.col,
                sourceLine(source_, tok.line));
    throw ParseError{};
}

// Skip tokens until we reach a likely statement boundary, so parsing can
// resume after a syntax error instead of cascading.
void Parser::synchronize() {
    while (!isAtEnd()) {
        if (previous().type == TokenType::Semicolon) return;
        switch (peek().type) {
            case TokenType::KwInt: case TokenType::KwString: case TokenType::KwBool:
            case TokenType::KwIf: case TokenType::KwWhile: case TokenType::KwPrint:
            case TokenType::KwReturn: case TokenType::KwFunc:
            case TokenType::KwFor: case TokenType::KwDo: case TokenType::KwVoid:
            case TokenType::KwBreak: case TokenType::KwContinue:
            case TokenType::RBrace:
                return;
            default:
                advance();
        }
    }
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>();
    while (!isAtEnd()) {
        size_t before = pos_;
        try {
            if (NodePtr node = topLevel()) program->statements.push_back(std::move(node));
            drainPending(program->statements);
        } catch (const ParseError&) {
            pending_.clear();
            synchronize();
        }
        // Progress guard: synchronize() can legitimately stop *on* a token it
        // won't consume (e.g. a stray top-level '}'), and statement() then fails
        // on that same token — an infinite loop. Force one token forward if a
        // whole iteration consumed nothing. Also cap runaway error floods.
        if (pos_ == before && !isAtEnd()) advance();
        if (errors_.size() > 100) break;
    }
    return program;
}

void Parser::enterNesting() {
    if (++nesting_ <= kMaxNesting) return;
    --nesting_;                               // the guard object never finished constructing
    if (!tooDeep_) {
        tooDeep_ = true;
        errors_.add("syntax", "TooDeeplyNested",
                    "Code is nested more than " + std::to_string(kMaxNesting) +
                    " levels deep (brackets, blocks or operators inside each other). "
                    "Split it into smaller pieces.",
                    peek().line, peek().col, sourceLine(source_, peek().line));
    }
    pos_ = tokens_.size() - 1;                // jump to end of file: stop parsing
    throw ParseError{};
}

void Parser::drainPending(std::vector<NodePtr>& into) {
    for (auto& n : pending_) into.push_back(std::move(n));
    pending_.clear();
}

NodePtr Parser::topLevel() {
    if (check(TokenType::KwFunc)) return funcDecl();
    if (check(TokenType::Identifier) && peek().value == "using" &&
        tokens_[pos_ + 1].type == TokenType::Identifier && tokens_[pos_ + 1].value == "namespace") {
        // `using namespace std;` from a C++ program: nothing to import in Lucid.
        while (!isAtEnd() && !check(TokenType::Semicolon)) advance();
        match(TokenType::Semicolon);
        return nullptr;
    }
    if (isCFuncStart()) {
        if (isPrototype()) {
            // `int add(int, int);` — C needs these; Lucid does not, because every
            // function is visible from the start. Accept it and move on.
            while (!check(TokenType::Semicolon)) advance();
            advance();
            return nullptr;
        }
        return cFuncDecl();
    }
    return statement();
}

// After `type name`, is the parameter list followed by ';' instead of a body?
bool Parser::isPrototype() const {
    size_t i = pos_ + 2;               // at '('
    int depth = 0;
    for (; i < tokens_.size(); ++i) {
        TokenType t = tokens_[i].type;
        if (t == TokenType::LParen) ++depth;
        else if (t == TokenType::RParen && --depth == 0) break;
        else if (t == TokenType::LBrace || t == TokenType::EndOfFile) return false;
    }
    return i + 1 < tokens_.size() && tokens_[i + 1].type == TokenType::Semicolon;
}

// Functions may only be declared at the top level. Report it once, then skip
// the whole declaration, header and body, so its contents cause no follow-on
// errors.
NodePtr Parser::skipNestedFunction() {
    const Token& start = peek();
    int ln = start.line;
    errors_.add("syntax", "UnexpectedToken",
                "functions can only be declared at the top level, not inside a block",
                start.line, start.col, sourceLine(source_, start.line));
    while (!isAtEnd() && !check(TokenType::LBrace) && !check(TokenType::Semicolon)) advance();
    if (match(TokenType::Semicolon)) return std::make_unique<Block>(ln);   // a prototype
    int depth = 0;
    while (!isAtEnd()) {
        TokenType t = advance().type;
        if (t == TokenType::LBrace) ++depth;
        else if (t == TokenType::RBrace && --depth == 0) break;
    }
    return std::make_unique<Block>(ln);
}

bool Parser::isCFuncStart() const {
    if (pos_ + 2 >= tokens_.size()) return false;
    TokenType t = peek().type;
    bool typeOrVoid = t == TokenType::KwInt || t == TokenType::KwString ||
                      t == TokenType::KwBool || t == TokenType::KwVoid;
    return typeOrVoid && tokens_[pos_ + 1].type == TokenType::Identifier &&
           tokens_[pos_ + 2].type == TokenType::LParen;
}

std::string Parser::returnTypeName() {
    if (match(TokenType::KwVoid)) return "void";
    return typeName();
}

std::string Parser::typeName() {
    if (match(TokenType::KwInt))    return "int";
    if (match(TokenType::KwString)) return "string";
    if (match(TokenType::KwBool))   return "bool";
    error(peek(), "expected a type (int, string, or bool) but found '" + peek().value + "'");
}

NodePtr Parser::statement() {
    switch (peek().type) {
        case TokenType::KwInt:
        case TokenType::KwString:
        case TokenType::KwBool:
            if (isCFuncStart()) return skipNestedFunction();
            return varDecl();
        case TokenType::KwVoid:
            if (isCFuncStart()) return skipNestedFunction();
            error(peek(), "'void' can only be used as a function's return type");
        case TokenType::KwDo:       return doWhileStatement();
        case TokenType::LBrace:     return block();          // a nested block { ... }
        case TokenType::KwBreak:
        case TokenType::KwContinue: return breakOrContinue();
        case TokenType::KwIf:     return ifStatement();
        case TokenType::KwWhile:  return whileStatement();
        case TokenType::KwFor:    return forStatement();
        case TokenType::KwPrint:  return printStatement();
        case TokenType::KwReturn: return returnStatement();
        case TokenType::KwFunc:   return skipNestedFunction();
        case TokenType::Identifier: return simpleStatement();
        case TokenType::PlusPlus:
        case TokenType::MinusMinus: {          // ++x;  --x;
            auto node = assignmentNoSemi();
            expect(TokenType::Semicolon, "';'");
            return node;
        }
        default:
            error(peek(), "expected a statement but found '" + peek().value + "'");
    }
}

std::unique_ptr<Block> Parser::block() {
    Nest nest(*this);
    int ln = peek().line;
    expect(TokenType::LBrace, "'{'");
    auto blk = std::make_unique<Block>(ln);
    while (!check(TokenType::RBrace) && !isAtEnd()) {
        size_t before = pos_;
        try {
            blk->statements.push_back(statement());
            drainPending(blk->statements);
        } catch (const ParseError&) {
            pending_.clear();
            synchronize();
        }
        if (pos_ == before && !check(TokenType::RBrace) && !isAtEnd()) advance();
    }
    expect(TokenType::RBrace, "'}'");
    return blk;
}

NodePtr Parser::varDecl() {
    std::string ty = typeName();
    NodePtr first = declarator(ty);
    while (match(TokenType::Comma)) pending_.push_back(declarator(ty));   // int a, b, c;
    expect(TokenType::Semicolon, "';'");
    return first;
}

// One `name` or `name = value` inside a declaration.
NodePtr Parser::declarator(const std::string& type) {
    const Token& name = expect(TokenType::Identifier, "a variable name");
    auto node = std::make_unique<VarDecl>(name.line);
    node->declType = type;
    node->name = name.value;
    if (match(TokenType::Assign)) node->init = expression();
    else node->init = defaultValue(type, name.line);   // int x;  ==>  int x = 0;
    return node;
}

// The value a declaration without an initialiser starts with.
NodePtr Parser::defaultValue(const std::string& type, int line) {
    if (type == "string") return std::make_unique<StringLiteral>(line);
    if (type == "bool")   return std::make_unique<BoolLiteral>(line);
    return std::make_unique<IntLiteral>(line);
}

NodePtr Parser::simpleStatement() {
    TokenType after = tokens_[pos_ + 1].type;
    if (check(TokenType::Identifier) &&
        (after == TokenType::Assign || isCompoundAssign(after) ||
         after == TokenType::PlusPlus || after == TokenType::MinusMinus)) {
        auto node = assignmentNoSemi();
        expect(TokenType::Semicolon, "';'");
        return node;
    }
    const Token& name = advance();  // the identifier
    // Common C++ habits get a direct explanation instead of a generic error.
    if (name.value == "cout" || (name.value == "std" && check(TokenType::Colon)))
        error(name, "Lucid prints with print(...), not cout: write print(x); "
                    "or print(\"x =\", x); for several values");
    if (name.value == "cin")
        error(name, "Lucid has no keyboard input (cin): give the variable its value in the program");
    if (check(TokenType::LParen)) {
        auto call = finishCall(name);
        expect(TokenType::Semicolon, "';'");
        return call;
    }
    error(peek(), "expected '=' or '(' after '" + name.value + "'");
}

NodePtr Parser::ifStatement() {
    int ln = peek().line;
    advance();  // 'if'
    expect(TokenType::LParen, "'(' after 'if'");
    auto cond = expression();
    expect(TokenType::RParen, "')'");
    auto thenB = block();
    std::unique_ptr<Block> elseB;
    if (match(TokenType::KwElse)) {
        if (check(TokenType::KwIf)) {
            // `else if` is sugar: the else branch is a block holding one If.
            int nested = peek().line;
            elseB = std::make_unique<Block>(nested);
            elseB->statements.push_back(ifStatement());
        } else {
            elseB = block();
        }
    }
    auto node = std::make_unique<If>(ln);
    node->cond = std::move(cond);
    node->thenBlock = std::move(thenB);
    node->elseBlock = std::move(elseB);
    return node;
}

bool Parser::isCompoundAssign(TokenType t) const {
    return t == TokenType::PlusAssign  || t == TokenType::MinusAssign ||
           t == TokenType::StarAssign  || t == TokenType::SlashAssign ||
           t == TokenType::PercentAssign;
}

bool Parser::isTypeStart() const {
    return check(TokenType::KwInt) || check(TokenType::KwString) || check(TokenType::KwBool);
}

// `x++` / `++x` (and --) desugar to `x = x + 1` (or - 1).
static NodePtr makeStep(const std::string& name, const std::string& op, int line) {
    auto lhs = std::make_unique<Identifier>(line);
    lhs->name = name;
    auto one = std::make_unique<IntLiteral>(line);
    one->value = 1;
    auto bin = std::make_unique<BinaryOp>(line);
    bin->op = op;
    bin->left = std::move(lhs);
    bin->right = std::move(one);
    auto node = std::make_unique<Assign>(line);
    node->name = name;
    node->value = std::move(bin);
    return node;
}

// `x = e`, a compound assignment desugared to `x = x op e`, or an
// increment / decrement (x++, ++x, x--, --x) desugared to `x = x +/- 1`.
NodePtr Parser::assignmentNoSemi() {
    if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {        // ++x / --x
        const Token& opTok = advance();
        const Token& target = expect(TokenType::Identifier, "a variable name after '" + opTok.value + "'");
        return makeStep(target.value, opTok.type == TokenType::PlusPlus ? "+" : "-", opTok.line);
    }
    const Token& name = expect(TokenType::Identifier, "a variable name");
    if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {        // x++ / x--
        const Token& opTok = advance();
        return makeStep(name.value, opTok.type == TokenType::PlusPlus ? "+" : "-", name.line);
    }
    auto node = std::make_unique<Assign>(name.line);
    node->name = name.value;

    if (match(TokenType::Assign)) {
        node->value = expression();
        return node;
    }

    static const std::unordered_map<int, std::string> ops = {
        {(int)TokenType::PlusAssign, "+"},  {(int)TokenType::MinusAssign, "-"},
        {(int)TokenType::StarAssign, "*"},  {(int)TokenType::SlashAssign, "/"},
        {(int)TokenType::PercentAssign, "%"},
    };
    auto it = ops.find((int)peek().type);
    if (it == ops.end()) error(peek(), "expected '=' after '" + name.value + "'");
    const Token& opTok = advance();

    // x += e  ==>  x = x + (e)
    auto bin = std::make_unique<BinaryOp>(opTok.line);
    bin->op = it->second;
    auto lhs = std::make_unique<Identifier>(name.line);
    lhs->name = name.value;
    bin->left = std::move(lhs);
    bin->right = expression();
    node->value = std::move(bin);
    return node;
}

// `for (init; cond; step) { body }` becomes `{ init; while (cond) { body } }`
// with `step` stored on the loop. Keeping the step separate (rather than
// appending it to the body) means `continue` cannot skip it.
NodePtr Parser::forStatement() {
    int ln = peek().line;
    advance();  // 'for'
    expect(TokenType::LParen, "'(' after 'for'");

    NodePtr init;
    std::vector<NodePtr> extraInits;                       // for (int i = 0, j = 9; ...)
    if (isTypeStart())      { init = varDecl(); drainPending(extraInits); }   // consumes its ';'
    else if (check(TokenType::Semicolon)) advance();       // empty initialiser
    else { init = assignmentNoSemi(); expect(TokenType::Semicolon, "';'"); }

    auto cond = expression();
    expect(TokenType::Semicolon, "';' after the loop condition");

    NodePtr step;
    if (!check(TokenType::RParen)) {
        step = assignmentNoSemi();
        if (check(TokenType::Comma)) {            // i++, j--  : several updates, in order
            auto steps = std::make_unique<Block>(step->line);
            steps->statements.push_back(std::move(step));
            while (match(TokenType::Comma)) steps->statements.push_back(assignmentNoSemi());
            step = std::move(steps);
        }
    }
    expect(TokenType::RParen, "')'");

    auto body = block();

    auto loop = std::make_unique<While>(ln);
    loop->cond = std::move(cond);
    loop->body = std::move(body);
    loop->step = std::move(step);     // runs after the body, and after `continue`

    auto wrapper = std::make_unique<Block>(ln);
    wrapper->scopeLabel = "for";
    if (init) wrapper->statements.push_back(std::move(init));
    for (auto& e : extraInits) wrapper->statements.push_back(std::move(e));
    wrapper->statements.push_back(std::move(loop));
    return wrapper;
}

NodePtr Parser::whileStatement() {
    int ln = peek().line;
    advance();  // 'while'
    expect(TokenType::LParen, "'(' after 'while'");
    auto cond = expression();
    expect(TokenType::RParen, "')'");
    auto body = block();
    auto node = std::make_unique<While>(ln);
    node->cond = std::move(cond);
    node->body = std::move(body);
    return node;
}

NodePtr Parser::printStatement() {
    int ln = peek().line;
    advance();  // 'print'
    expect(TokenType::LParen, "'(' after 'print'");
    auto node = std::make_unique<Print>(ln);
    if (!check(TokenType::RParen)) {
        do { node->args.push_back(expression()); } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "')'");
    expect(TokenType::Semicolon, "';'");
    return node;
}

NodePtr Parser::returnStatement() {
    int ln = peek().line;
    advance();  // 'return'
    NodePtr value;
    if (!check(TokenType::Semicolon)) value = expression();   // `return;` in a void function
    expect(TokenType::Semicolon, "';'");
    auto node = std::make_unique<Return>(ln);
    node->value = std::move(value);
    return node;
}

NodePtr Parser::funcDecl() {
    int ln = peek().line;
    advance();  // 'func'
    const Token& name = expect(TokenType::Identifier, "a function name");
    auto node = std::make_unique<FuncDecl>(ln);
    node->name = name.value;
    parseParams(*node);
    expect(TokenType::Colon, "':' before the return type");
    node->returnType = returnTypeName();
    node->body = block();
    return node;
}

// C-style declaration: the return type comes first, as in C.
NodePtr Parser::cFuncDecl() {
    int ln = peek().line;
    std::string ret = returnTypeName();
    const Token& name = expect(TokenType::Identifier, "a function name");
    auto node = std::make_unique<FuncDecl>(ln);
    node->name = name.value;
    node->returnType = ret;
    parseParams(*node);
    node->body = block();
    return node;
}

void Parser::parseParams(FuncDecl& fn) {
    expect(TokenType::LParen, "'(' after the function name");
    if (check(TokenType::KwVoid) && tokens_[pos_ + 1].type == TokenType::RParen) {
        advance();   // f(void) means no parameters, as in C
    } else if (!check(TokenType::RParen)) {
        do {
            std::string pty = typeName();
            const Token& pname = expect(TokenType::Identifier, "a parameter name");
            fn.params.push_back(Param{pty, pname.value});
        } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "')'");
}

NodePtr Parser::doWhileStatement() {
    int ln = peek().line;
    advance();  // 'do'
    auto body = block();
    expect(TokenType::KwWhile, "'while' after the do-block");
    expect(TokenType::LParen, "'(' after 'while'");
    auto cond = expression();
    expect(TokenType::RParen, "')'");
    expect(TokenType::Semicolon, "';' after do-while");
    auto node = std::make_unique<While>(ln);
    node->cond = std::move(cond);
    node->body = std::move(body);
    node->doWhile = true;
    return node;
}

NodePtr Parser::breakOrContinue() {
    const Token& kw = advance();
    expect(TokenType::Semicolon, "';' after '" + kw.value + "'");
    if (kw.type == TokenType::KwBreak) return std::make_unique<Break>(kw.line);
    return std::make_unique<Continue>(kw.line);
}

// ---- Expressions ---------------------------------------------------------
NodePtr Parser::expression() {
    Nest nest(*this);
    return conditional();
}

// c ? a : b — lowest-precedence expression, right-associative, so
// a ? b : c ? d : e  means  a ? b : (c ? d : e).
NodePtr Parser::conditional() {
    auto cond = logicalOr();
    if (!check(TokenType::Question)) return cond;
    int ln = advance().line;
    auto node = std::make_unique<Conditional>(ln);
    node->cond = std::move(cond);
    node->thenExpr = expression();
    expect(TokenType::Colon, "':' in the ?: expression");
    node->elseExpr = conditional();
    return node;
}

NodePtr Parser::logicalOr() {
    auto left = logicalAnd();
    while (check(TokenType::OrOr)) {
        Token op = advance();
        auto right = logicalAnd();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::logicalAnd() {
    auto left = equality();
    while (check(TokenType::AndAnd)) {
        Token op = advance();
        auto right = equality();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::equality() {
    auto left = comparison();
    while (check(TokenType::EqEq) || check(TokenType::NotEq)) {
        Token op = advance();
        auto right = comparison();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::comparison() {
    auto left = term();
    while (check(TokenType::Lt) || check(TokenType::Gt) ||
           check(TokenType::Le) || check(TokenType::Ge)) {
        Token op = advance();
        auto right = term();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::term() {
    auto left = factor();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        Token op = advance();
        auto right = factor();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::factor() {
    auto left = unary();
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        Token op = advance();
        auto right = unary();
        auto node = std::make_unique<BinaryOp>(op.line);
        node->op = op.value; node->left = std::move(left); node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

NodePtr Parser::unary() {
    Nest nest(*this);
    if (check(TokenType::Not) || check(TokenType::Minus)) {
        Token op = advance();
        auto operand = unary();
        auto node = std::make_unique<UnaryOp>(op.line);
        node->op = op.value;
        node->operand = std::move(operand);
        return node;
    }
    return primary();
}

NodePtr Parser::finishCall(const Token& name) {
    expect(TokenType::LParen, "'('");
    auto call = std::make_unique<Call>(name.line);
    call->callee = name.value;
    if (!check(TokenType::RParen)) {
        do {
            call->args.push_back(expression());
        } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "')'");
    return call;
}

NodePtr Parser::primary() {
    const Token& tok = peek();
    switch (tok.type) {
        case TokenType::IntLiteral: {
            advance();
            auto node = std::make_unique<IntLiteral>(tok.line);
            try {
                node->value = std::stoi(tok.value);
            } catch (const std::out_of_range&) {
                node->value = 0;
                errors_.add("syntax", "IntegerOverflow",
                            "Integer literal '" + tok.value + "' is too large for int.",
                            tok.line, tok.col, sourceLine(source_, tok.line));
            }
            return node;
        }
        case TokenType::StringLiteral: {
            advance();
            auto node = std::make_unique<StringLiteral>(tok.line);
            node->value = tok.value;
            return node;
        }
        case TokenType::KwTrue:
        case TokenType::KwFalse: {
            advance();
            auto node = std::make_unique<BoolLiteral>(tok.line);
            node->value = (tok.type == TokenType::KwTrue);
            return node;
        }
        case TokenType::Identifier: {
            advance();
            if (check(TokenType::LParen)) return finishCall(tok);
            auto node = std::make_unique<Identifier>(tok.line);
            node->name = tok.value;
            return node;
        }
        case TokenType::LParen: {
            advance();
            auto expr = expression();
            expect(TokenType::RParen, "')'");
            return expr;
        }
        default:
            error(tok, "expected an expression but found '" +
                       (tok.type == TokenType::EndOfFile ? "end of file" : tok.value) + "'");
    }
}

} // namespace lucid
