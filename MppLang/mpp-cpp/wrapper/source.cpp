// MppxxCpp wrapper — reading M++ source: which bytes are code, and tokens.

#include "source.hpp"

namespace mppxx {

std::vector<bool> codeMask(std::string_view s) {
    const std::size_t n = s.size();
    std::vector<bool> mask(n, false);
    struct Frame { bool code; int depth = 0; std::size_t hashes = 0; bool multi = false; };
    std::vector<Frame> stack{Frame{true}};
    auto at = [&](std::size_t i, std::string_view lit) { return i + lit.size() <= n && s.substr(i, lit.size()) == lit; };
    std::size_t i = 0;
    while (i < n) {
        Frame& top = stack.back();
        if (top.code) {
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') ++i; continue; }
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
                int depth = 1;
                i += 2;
                while (i < n && depth > 0) {
                    if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { ++depth; i += 2; }
                    else if (s[i] == '*' && i + 1 < n && s[i + 1] == '/') { --depth; i += 2; }
                    else ++i;
                }
                continue;
            }
            std::size_t j = i;
            while (j < n && s[j] == '#') ++j;
            if (j < n && s[j] == '"') {
                const bool multi = at(j, "\"\"\"");
                stack.push_back(Frame{false, 0, j - i, multi});
                i = j + (multi ? 3 : 1);
                continue;
            }
            mask[i] = true;
            if (s[i] == '(') ++top.depth;
            else if (s[i] == ')') {
                if (top.depth == 0 && stack.size() > 1) { mask[i] = false; stack.pop_back(); ++i; continue; }
                --top.depth;
            }
            ++i;
        } else {
            std::string closer = top.multi ? "\"\"\"" : "\"";
            closer.append(top.hashes, '#');
            std::string esc = "\\";
            esc.append(top.hashes, '#');
            if (at(i, esc)) {
                const std::size_t k = i + esc.size();
                if (k < n && s[k] == '(') { stack.push_back(Frame{true}); i = k + 1; continue; }
                i = k + 1;
                continue;
            }
            if (at(i, closer)) { i += closer.size(); stack.pop_back(); continue; }
            ++i;
        }
    }
    return mask;
}

std::vector<Token> tokenize(std::string_view s) {
    const std::vector<bool> mask = codeMask(s);
    const std::size_t n = s.size();
    std::vector<Token> out;
    int line = 1;
    std::size_t lineAt = 0;
    auto lineOf = [&](std::size_t i) {
        while (lineAt < i) { if (s[lineAt] == '\n') ++line; ++lineAt; }
        return line;
    };
    auto isOp = [](char c) { return std::string_view("=+-*/%<>!&|^~?.").find(c) != std::string_view::npos; };
    std::size_t i = 0;
    while (i < n) {
        if (!mask[i]) {
            // A string literal starts with a quote (or #"); anything else outside code is a comment.
            const std::size_t start = i;
            while (i < n && !mask[i]) ++i;
            if (s[start] == '"' || s[start] == '#') out.push_back(Token{"\"…\"", lineOf(start), true});
            continue;
        }
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
        if (identStart(static_cast<unsigned char>(c))) {
            std::size_t j = i + 1;
            while (j < n && identChar(static_cast<unsigned char>(s[j]))) ++j;
            out.push_back(Token{std::string(s.substr(i, j - i)), lineOf(i)});
            i = j;
            continue;
        }
        if (c >= '0' && c <= '9') {
            std::size_t j = i + 1;
            while (j < n && mask[j] && (identChar(static_cast<unsigned char>(s[j])) || (s[j] == '.' && j + 1 < n && s[j + 1] >= '0' && s[j + 1] <= '9'))) ++j;
            out.push_back(Token{std::string(s.substr(i, j - i)), lineOf(i)});
            i = j;
            continue;
        }
        if (isOp(c)) {
            std::size_t j = i + 1;
            while (j < n && mask[j] && isOp(s[j])) ++j;
            out.push_back(Token{std::string(s.substr(i, j - i)), lineOf(i)});
            i = j;
            continue;
        }
        out.push_back(Token{std::string(1, c), lineOf(i)});
        ++i;
    }
    return out;
}

}  // namespace mppxx
