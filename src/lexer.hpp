// ============================================================================
// lexer.hpp — Phase 1 of Lucid.
// Input : raw source text (std::string)
// Output: std::vector<Token>  (+ lexical errors into an ErrorList)
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include "token.hpp"
#include "error.hpp"

namespace lucid {

class Lexer {
public:
    Lexer(const std::string& source, ErrorList& errors);

    // Scan the whole source into tokens. Always ends with an EndOfFile token.
    std::vector<Token> tokenize();

private:
    const std::string& src_;
    ErrorList& errors_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    bool atEnd() const { return pos_ >= src_.size(); }
    char peek() const { return atEnd() ? '\0' : src_[pos_]; }
    char peekNext() const { return (pos_ + 1 >= src_.size()) ? '\0' : src_[pos_ + 1]; }
    char advance();                 // consume one char, updating line/col
    bool match(char expected);      // consume if next char == expected

    void skipWhitespaceAndComments();
    // Length in bytes of a well-formed multi-byte UTF-8 character at pos_
    // (0 if there is none) and its code point.
    int unicodeAt(long& codePoint) const;
    bool atCurlyDoubleQuote() const;
    bool atLineStart() const;                    // only blanks before pos_ on this line             // “ or ” pasted from a word processor
    void reportNonAscii(int startLine, int startCol);
    Token makeString();
    Token makeNumber(int startLine, int startCol);
    Token makeIdentifierOrKeyword(int startLine, int startCol);
};

} // namespace lucid
