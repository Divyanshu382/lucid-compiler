// ============================================================================
// token.hpp — Shared token vocabulary for the Lucid compiler.
// Phase: (shared)  Input: n/a  Output: Token struct + TokenType enum.
// Produced by the Lexer, consumed by the Parser. No logic lives here — just
// the data types every phase agrees on.
// ============================================================================
#pragma once

#include <string>
#include "../third_party/json.hpp"

namespace lucid {

// Every distinct thing the lexer can emit.
enum class TokenType {
    // Literals & names
    Identifier,
    IntLiteral,
    StringLiteral,

    // Keywords
    KwInt, KwString, KwBool,
    KwIf, KwElse, KwWhile,
    KwPrint, KwFunc, KwReturn,
    KwTrue, KwFalse, KwFor,
    KwBreak, KwContinue, KwDo, KwVoid,

    // Operators
    Plus, Minus, Star, Slash, Percent,   // + - * / %
    Lt, Gt, Le, Ge, EqEq, NotEq,         // < > <= >= == !=
    AndAnd, OrOr, Not,                   // && || !
    Assign,                              // =
    PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,  // += -= *= /= %=
    PlusPlus, MinusMinus,                // ++ --
    Question,                            // ? (in a ? b : c)

    // Punctuation
    LBrace, RBrace,                      // { }
    LParen, RParen,                      // ( )
    Semicolon, Colon, Comma,             // ; : ,

    // Sentinel
    EndOfFile
};

// A lexeme with its source position (1-based line and column).
struct Token {
    TokenType type;
    std::string value;   // the raw text, e.g. "x", "5", "hello", "+"
    int line;
    int col;
};

// Human-readable name for a token type — used by the UI token table and by
// parser error messages ("expected ';' but found ...").
inline const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::Identifier:    return "Identifier";
        case TokenType::IntLiteral:    return "IntLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::KwInt:         return "Keyword(int)";
        case TokenType::KwString:      return "Keyword(string)";
        case TokenType::KwBool:        return "Keyword(bool)";
        case TokenType::KwIf:          return "Keyword(if)";
        case TokenType::KwElse:        return "Keyword(else)";
        case TokenType::KwWhile:       return "Keyword(while)";
        case TokenType::KwPrint:       return "Keyword(print)";
        case TokenType::KwFunc:        return "Keyword(func)";
        case TokenType::KwReturn:      return "Keyword(return)";
        case TokenType::KwTrue:        return "Keyword(true)";
        case TokenType::KwFalse:       return "Keyword(false)";
        case TokenType::Plus:          return "Plus";
        case TokenType::Minus:         return "Minus";
        case TokenType::Star:          return "Star";
        case TokenType::Slash:         return "Slash";
        case TokenType::Percent:       return "Percent";
        case TokenType::Lt:            return "Less";
        case TokenType::Gt:            return "Greater";
        case TokenType::Le:            return "LessEqual";
        case TokenType::Ge:            return "GreaterEqual";
        case TokenType::EqEq:          return "EqualEqual";
        case TokenType::NotEq:         return "NotEqual";
        case TokenType::AndAnd:        return "And";
        case TokenType::OrOr:          return "Or";
        case TokenType::Not:           return "Not";
        case TokenType::Assign:        return "Assign";
        case TokenType::KwFor:         return "Keyword(for)";
        case TokenType::KwBreak:       return "Keyword(break)";
        case TokenType::KwContinue:    return "Keyword(continue)";
        case TokenType::KwDo:          return "Keyword(do)";
        case TokenType::KwVoid:        return "Keyword(void)";
        case TokenType::Question:      return "Question";
        case TokenType::PlusAssign:    return "PlusAssign";
        case TokenType::MinusAssign:   return "MinusAssign";
        case TokenType::StarAssign:    return "StarAssign";
        case TokenType::SlashAssign:   return "SlashAssign";
        case TokenType::PercentAssign: return "PercentAssign";
        case TokenType::PlusPlus:      return "PlusPlus";
        case TokenType::MinusMinus:    return "MinusMinus";
        case TokenType::LBrace:        return "LBrace";
        case TokenType::RBrace:        return "RBrace";
        case TokenType::LParen:        return "LParen";
        case TokenType::RParen:        return "RParen";
        case TokenType::Semicolon:     return "Semicolon";
        case TokenType::Colon:         return "Colon";
        case TokenType::Comma:         return "Comma";
        case TokenType::EndOfFile:     return "EndOfFile";
    }
    return "Unknown";
}

// A short display label for what a token type "looks like" — used to render
// "expected ')'" style messages in the parser.
inline std::string tokenTypeLexeme(TokenType t) {
    switch (t) {
        case TokenType::Semicolon: return ";";
        case TokenType::Colon:     return ":";
        case TokenType::Comma:     return ",";
        case TokenType::LBrace:    return "{";
        case TokenType::RBrace:    return "}";
        case TokenType::LParen:    return "(";
        case TokenType::RParen:    return ")";
        case TokenType::Assign:    return "=";
        case TokenType::PlusAssign:    return "+=";
        case TokenType::MinusAssign:   return "-=";
        case TokenType::StarAssign:    return "*=";
        case TokenType::SlashAssign:   return "/=";
        case TokenType::PercentAssign: return "%=";
        case TokenType::PlusPlus:      return "++";
        case TokenType::Question:      return "?";
        case TokenType::MinusMinus:    return "--";
        default:                   return tokenTypeName(t);
    }
}

inline nlohmann::json tokenToJson(const Token& tk) {
    return nlohmann::json{
        {"type", tokenTypeName(tk.type)},
        {"value", tk.value},
        {"line", tk.line},
        {"col", tk.col}
    };
}

} // namespace lucid
