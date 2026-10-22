// ============================================================================
// lexer.cpp — Phase 1 of Lucid.  text -> tokens.
// Skips whitespace and // comments while tracking (line, col). Reports two
// lexical errors: unknown character and unterminated string.
// ============================================================================
#include "lexer.hpp"

#include <cstdio>
#include <cctype>
#include <unordered_map>

namespace lucid {

// The reserved words of Lucid mapped to their token types.
static const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> kw = {
        {"int", TokenType::KwInt},       {"string", TokenType::KwString},
        {"bool", TokenType::KwBool},     {"if", TokenType::KwIf},
        {"else", TokenType::KwElse},     {"while", TokenType::KwWhile},
        {"print", TokenType::KwPrint},   {"func", TokenType::KwFunc},
        {"return", TokenType::KwReturn}, {"true", TokenType::KwTrue},
        {"false", TokenType::KwFalse},  {"for", TokenType::KwFor},
        {"break", TokenType::KwBreak},   {"continue", TokenType::KwContinue},
        {"do", TokenType::KwDo},         {"void", TokenType::KwVoid},
        {"const", TokenType::KwConst},
    };
    return kw;
}

Lexer::Lexer(const std::string& source, ErrorList& errors)
    : src_(source), errors_(errors) {}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    // A UTF-8 continuation byte (10xxxxxx) is part of the previous character,
    // so columns count characters, matching what the editor shows.
    else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) { ++col_; }
    return c;
}

namespace {

// Bytes in the UTF-8 sequence led by `b`: 1 for ASCII, 0 if `b` cannot lead one.
int utf8Length(unsigned char b) {
    if (b < 0x80) return 1;
    if (b >= 0xC2 && b <= 0xDF) return 2;
    if (b >= 0xE0 && b <= 0xEF) return 3;
    if (b >= 0xF0 && b <= 0xF4) return 4;
    return 0;
}

// Spaces that look identical to ' ' but are different characters. They arrive
// when code is copied from PDFs, Word, web pages or chat apps; Windows Notepad
// can also start a file with U+FEFF (byte-order mark).
bool isUnicodeSpace(long cp) {
    return cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200B) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000 || cp == 0xFEFF;
}

std::string codePointName(long cp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04lX", cp);
    return buf;
}

// Characters that word processors substitute for the ASCII ones Lucid uses.
std::string lookalikeHint(long cp) {
    switch (cp) {
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212:
                     return " Did you mean '-'?";
        case 0x00D7: return " Did you mean '*'?";
        case 0x00F7: return " Did you mean '/'?";
        case 0x2264: return " Did you mean '<='?";
        case 0x2265: return " Did you mean '>='?";
        case 0x2260: return " Did you mean '!='?";
        case 0x2018: case 0x2019:
                     return " Strings use double quotes: \"like this\".";
        case 0xFF1B: return " Did you mean ';'?";
        default:     return "";
    }
}

} // namespace

// True when only spaces or tabs come before pos_ on its line.
bool Lexer::atLineStart() const {
    for (size_t i = pos_; i > 0; --i) {
        char p = src_[i - 1];
        if (p == '\n') return true;
        if (p != ' ' && p != '\t' && p != '\r') return false;
    }
    return true;
}

int Lexer::unicodeAt(long& codePoint) const {
    if (atEnd()) return 0;
    unsigned char lead = static_cast<unsigned char>(src_[pos_]);
    int n = utf8Length(lead);
    if (n < 2 || pos_ + n > src_.size()) return 0;
    long cp = lead & (0xFF >> (n + 1));
    for (int i = 1; i < n; ++i) {
        unsigned char b = static_cast<unsigned char>(src_[pos_ + i]);
        if ((b & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (b & 0x3F);
    }
    codePoint = cp;
    return n;
}

bool Lexer::atCurlyDoubleQuote() const {
    long cp = 0;
    return unicodeAt(cp) == 3 && (cp == 0x201C || cp == 0x201D);
}

void Lexer::reportNonAscii(int startLine, int startCol) {
    long cp = 0;
    int n = unicodeAt(cp);
    if (n == 0) {
        // Not valid UTF-8 (e.g. a file saved in an old Windows code page).
        char buf[8];
        std::snprintf(buf, sizeof buf, "0x%02X", static_cast<unsigned char>(src_[pos_]));
        advance();
        errors_.add("lexical", "InvalidEncoding",
                    std::string("Invalid text encoding (byte ") + buf + "). Save the file as UTF-8.",
                    startLine, startCol, "");
        return;
    }
    std::string ch = src_.substr(pos_, n);
    for (int i = 0; i < n; ++i) advance();
    errors_.add("lexical", "UnknownCharacter",
                "Unexpected character '" + ch + "' (" + codePointName(cp) + ")." + lookalikeHint(cp),
                startLine, startCol, sourceLine(src_, startLine));
}

bool Lexer::match(char expected) {
    if (atEnd() || src_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        long cp = 0;
        int n = 0;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            advance();
        } else if (c == '/' && peekNext() == '/') {
            // line comment — run to end of line (or EOF)
            while (!atEnd() && peek() != '\n') advance();
        } else if (c == '#' && atLineStart()) {
            // #include <iostream> etc. Lucid needs no preprocessor or headers,
            // so a C++ program's directive lines are skipped like comments.
            while (!atEnd() && peek() != '\n') advance();
        } else if (c == '/' && peekNext() == '*') {
            // block comment — run to the closing */ (may span lines)
            int startLine = line_, startCol = col_;
            advance(); advance();
            while (!atEnd() && !(peek() == '*' && peekNext() == '/')) advance();
            if (atEnd()) {
                errors_.add("lexical", "UnterminatedComment",
                            "Block comment '/*' is never closed with '*/'.",
                            startLine, startCol, sourceLine(src_, startLine));
                return;
            }
            advance(); advance();
        } else if ((n = unicodeAt(cp)) > 0 && isUnicodeSpace(cp)) {
            for (int i = 0; i < n; ++i) advance();   // an invisible space is still a space
        } else {
            return;
        }
    }
}

Token Lexer::makeString() {
    int startLine = line_, startCol = col_;
    // Opening quote: '"', or a curly “ ” (3 bytes) pasted from a word processor.
    if (peek() == '"') advance(); else for (int i = 0; i < 3; ++i) advance();
    std::string value;
    while (!atEnd() && peek() != '"' && !atCurlyDoubleQuote() && peek() != '\n') {
        char c = advance();
        if (c == '\\' && !atEnd()) {
            char esc = advance();
            switch (esc) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                default:   value += esc;  break;  // unknown escape: keep literal
            }
        } else {
            value += c;
        }
    }
    if (atCurlyDoubleQuote()) {
        for (int i = 0; i < 3; ++i) advance();   // closing curly quote
        return Token{TokenType::StringLiteral, value, startLine, startCol};
    }
    if (peek() != '"') {
        errors_.add("lexical", "UnterminatedString",
                    "String literal is missing its closing '\"'.",
                    startLine, startCol, sourceLine(src_, startLine));
        // Return a best-effort string token so the parser can keep going.
        return Token{TokenType::StringLiteral, value, startLine, startCol};
    }
    advance();  // consume closing quote
    return Token{TokenType::StringLiteral, value, startLine, startCol};
}

Token Lexer::makeCharLiteral() {
    int startLine = line_, startCol = col_;
    advance();  // opening '
    std::string value;
    if (peek() == '\\' && !atEnd()) {
        advance();
        char esc = atEnd() ? '\0' : advance();
        switch (esc) {
            case 'n':  value = "\n"; break;
            case 't':  value = "\t"; break;
            case '0':  value = std::string(1, '\0'); break;
            default:   value = std::string(1, esc); break;   // \' \" \\ and the rest
        }
    } else if (!atEnd() && peek() != '\'' && peek() != '\n') {
        value += advance();
        while (!atEnd() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80) value += advance();  // whole UTF-8 char
    }
    if (!match('\'')) {
        errors_.add("lexical", "UnterminatedChar", "Character literal is missing its closing '.",
                    startLine, startCol, sourceLine(src_, startLine));
    }
    return Token{TokenType::StringLiteral, value, startLine, startCol};
}

Token Lexer::makeNumber(int startLine, int startCol) {
    std::string num;
    while (std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
    return Token{TokenType::IntLiteral, num, startLine, startCol};
}

Token Lexer::makeIdentifierOrKeyword(int startLine, int startCol) {
    std::string text;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        text += advance();
    auto it = keywords().find(text);
    TokenType type = (it != keywords().end()) ? it->second : TokenType::Identifier;
    return Token{type, text, startLine, startCol};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skipWhitespaceAndComments();
        if (atEnd()) break;

        int startLine = line_, startCol = col_;
        char c = peek();

        // `std::` names (std::cout, std::string, ...) are the same names
        // without the prefix: Lucid has a single namespace.
        if (src_.compare(pos_, 5, "std::") == 0) {
            for (int i = 0; i < 5; ++i) advance();
            continue;
        }
        // Character literal: 'a', '\n'. Lucid has no char type, so it becomes
        // a one-character string, which is all `cout << '\n'` needs.
        if (c == '\'') {
            tokens.push_back(makeCharLiteral());
            continue;
        }
        // Identifiers / keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(makeIdentifierOrKeyword(startLine, startCol));
            continue;
        }
        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(makeNumber(startLine, startCol));
            continue;
        }
        // Strings
        if (c == '"' || atCurlyDoubleQuote()) {
            tokens.push_back(makeString());
            continue;
        }
        // Any other non-ASCII character: report it once, as a whole character.
        if (static_cast<unsigned char>(c) >= 0x80) {
            reportNonAscii(startLine, startCol);
            continue;
        }

        // Operators & punctuation
        advance();  // consume the first char of the operator
        auto emit = [&](TokenType t, const std::string& v) {
            tokens.push_back(Token{t, v, startLine, startCol});
        };
        switch (c) {
            case '+':
                if (match('=')) emit(TokenType::PlusAssign, "+=");
                else if (match('+')) emit(TokenType::PlusPlus, "++");
                else emit(TokenType::Plus, "+");
                break;
            case '-':
                if (match('=')) emit(TokenType::MinusAssign, "-=");
                else if (match('-')) emit(TokenType::MinusMinus, "--");
                else emit(TokenType::Minus, "-");
                break;
            case '*': if (match('=')) emit(TokenType::StarAssign, "*="); else emit(TokenType::Star, "*"); break;
            case '/': if (match('=')) emit(TokenType::SlashAssign, "/="); else emit(TokenType::Slash, "/"); break;
            case '%': if (match('=')) emit(TokenType::PercentAssign, "%="); else emit(TokenType::Percent, "%"); break;
            case '(': emit(TokenType::LParen, "("); break;
            case ')': emit(TokenType::RParen, ")"); break;
            case '{': emit(TokenType::LBrace, "{"); break;
            case '}': emit(TokenType::RBrace, "}"); break;
            case ';': emit(TokenType::Semicolon, ";"); break;
            case ':': emit(TokenType::Colon, ":"); break;
            case '?': emit(TokenType::Question, "?"); break;
            case ',': emit(TokenType::Comma, ","); break;
            case '<':
                if (match('<')) emit(TokenType::LShift, "<<");
                else if (match('=')) emit(TokenType::Le, "<=");
                else emit(TokenType::Lt, "<");
                break;
            case '>':
                if (match('>')) emit(TokenType::RShift, ">>");
                else if (match('=')) emit(TokenType::Ge, ">=");
                else emit(TokenType::Gt, ">");
                break;
            case '=': if (match('=')) emit(TokenType::EqEq, "=="); else emit(TokenType::Assign, "="); break;
            case '!': if (match('=')) emit(TokenType::NotEq, "!="); else emit(TokenType::Not, "!"); break;
            case '&':
                if (match('&')) emit(TokenType::AndAnd, "&&");
                else errors_.add("lexical", "UnknownCharacter",
                                 "Unexpected '&'. Did you mean '&&'?",
                                 startLine, startCol, sourceLine(src_, startLine));
                break;
            case '|':
                if (match('|')) emit(TokenType::OrOr, "||");
                else errors_.add("lexical", "UnknownCharacter",
                                 "Unexpected '|'. Did you mean '||'?",
                                 startLine, startCol, sourceLine(src_, startLine));
                break;
            default: {
                unsigned char u = static_cast<unsigned char>(c);
                std::string what;
                if (u < 0x20 || u == 0x7F) {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "U+%04X", u);
                    what = std::string("an invisible control character (") + buf + ")";
                } else {
                    what = std::string("'") + c + "'";
                }
                errors_.add("lexical", "UnknownCharacter", "Unexpected character " + what + ".",
                            startLine, startCol, sourceLine(src_, startLine));
                break;
            }
        }
    }

    tokens.push_back(Token{TokenType::EndOfFile, "", line_, col_});
    return tokens;
}

} // namespace lucid
