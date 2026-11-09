// MppxxCpp wrapper — reading M++ source: which bytes are code, and tokens.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mppxx {

/// true for bytes that are M++ code (not comments or string literals;
/// string interpolations count as code).
std::vector<bool> codeMask(std::string_view s);

inline bool identStart(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
inline bool identChar(unsigned char c) { return identStart(c) || (c >= '0' && c <= '9'); }

struct Token {
    std::string text;
    int line = 0;
    bool opaque = false;      // a string literal (comments are dropped)
};

/// The file's tokens: identifiers, numbers, maximal runs of operator
/// characters, single brackets; string literals as one opaque token.
std::vector<Token> tokenize(std::string_view s);

}  // namespace mppxx
