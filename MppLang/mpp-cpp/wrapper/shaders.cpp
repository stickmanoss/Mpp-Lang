// MppxxCpp wrapper — compiling Metallic shaders to C++23.
//
// @kernel and @fragment functions, the structs they use and the helper
// functions they call are translated into C++ against
// MppLang/mpp-metallic/Core/MetallicShader.hpp, where float4 is a clang
// vector type. The translation is deliberately narrow: anything outside the
// shading subset (closures, optionals, strings, switch, bit operators whose
// precedence differs from C's, ...) leaves that shader in M++, where it runs
// as before. So does any shader in a file whose C++ fails to compile.

#include "mppxx.h"
#include "source.hpp"
#include "wrapper.hpp"

#include <algorithm>
#include <map>
#include <functional>
#include <set>

namespace mppxx {
namespace {

struct Tok {
    std::string text;
    int line;
    bool isString;
};
using Toks = std::vector<Tok>;
struct Range { std::size_t lo = 0, hi = 0; bool empty() const { return lo >= hi; } };

struct Param {
    std::optional<std::string> label;
    std::string type, attribute, name;
};
struct Field { std::string name, type, swiftType; bool flat; };
struct Struct {
    std::string name;
    std::vector<Field> fields;
    std::optional<std::string> position;
    bool hasDefaults = false;            // memberwise inits may skip fields: not translated
};
struct Func {
    std::string name, kind;              // kind: "", "kernel", "fragment", "vertex", "overloaded"
    std::vector<Param> params;
    Strings result;                      // return type tokens (empty: Void)
    Range body;                          // token indices inside the braces
};
struct NativeShader {
    std::string symbol;
    std::vector<std::pair<int, std::string>> buffers;   // [[buffer(n)]]: (index, Swift type)
    std::vector<int> textures;
};

using Structs = std::map<std::string, Struct>;
using Funcs = std::map<std::string, Func>;

bool contains(std::initializer_list<std::string_view> list, std::string_view s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

const std::set<std::string> cppKeywords{
    "auto", "char", "class", "const", "default", "delete", "double", "float", "int", "long", "new", "namespace",
    "operator", "private", "protected", "public", "register", "short", "signed", "sizeof", "template", "this",
    "typename", "union", "unsigned", "virtual", "goto", "friend", "explicit", "export", "extern",
    "mutable", "asm", "bool", "concept", "requires", "module", "import", "co_await", "co_return", "co_yield",
    "constexpr", "consteval", "constinit", "decltype", "noexcept", "nullptr", "static_assert", "thread_local",
    "typedef", "typeid", "using", "wchar_t", "char8_t", "char16_t", "char32_t", "alignas", "alignof", "and",
    "or", "not", "xor", "bitand", "bitor", "compl", "not_eq", "and_eq", "or_eq", "xor_eq", "pi", "mtl", "std",
    "thread", "half", "uint", "vec2", "vec3", "vec4", "uvec2", "ivec2", "sum", "runRows", "shadeSpan"};

std::string cppName(const std::string& n) { return cppKeywords.count(n) ? n + "_" : n; }

const std::set<std::string> shaderMath{
    "length", "length_squared", "normalize", "dot", "cross", "distance", "mix", "smoothstep", "step", "clamp",
    "saturate", "min", "max", "abs", "sign", "fract", "radians", "degrees", "sqrt", "rsqrt", "sin", "cos", "tan",
    "asin", "acos", "atan", "atan2", "exp", "exp2", "log", "log2", "floor", "ceil", "round", "trunc", "pow", "fmod"};

const std::map<std::string, std::string> constructors{
    {"Float", "float"}, {"float", "float"}, {"half", "float"}, {"Float32", "float"}, {"Double", "double"}, {"Int", "long"},
    {"Int64", "long"}, {"Int32", "int32_t"}, {"UInt32", "uint32_t"}, {"uint", "uint32_t"}, {"Bool", "bool"},
    {"float2", "vec2"}, {"half2", "vec2"}, {"float3", "vec3"}, {"half3", "vec3"}, {"float4", "vec4"}, {"half4", "vec4"},
    {"uint2", "uvec2"}, {"int2", "ivec2"}};

/// The C++ spelling of an M++ type, or nullopt if shaders can't use it.
std::optional<std::string> cppType(const std::string& t, const Structs& structs) {
    static const std::map<std::string, std::string> types{
        {"Float", "float"}, {"float", "float"}, {"half", "float"}, {"Float32", "float"}, {"Double", "double"}, {"Float64", "double"},
        {"Int", "long"}, {"Int64", "long"}, {"Int32", "int32_t"}, {"UInt32", "uint32_t"}, {"uint", "uint32_t"}, {"Bool", "bool"},
        {"float2", "float2"}, {"half2", "float2"}, {"SIMD2<Float>", "float2"}, {"float3", "float3"}, {"half3", "float3"},
        {"SIMD3<Float>", "float3"}, {"float4", "float4"}, {"half4", "float4"}, {"SIMD4<Float>", "float4"},
        {"int2", "int2"}, {"SIMD2<Int32>", "int2"}, {"int3", "int3"}, {"SIMD3<Int32>", "int3"}, {"int4", "int4"}, {"SIMD4<Int32>", "int4"},
        {"uint2", "uint2"}, {"SIMD2<UInt32>", "uint2"}, {"uint3", "uint3"}, {"SIMD3<UInt32>", "uint3"}, {"uint4", "uint4"},
        {"SIMD4<UInt32>", "uint4"}, {"sampler", "sampler"}};
    if (const auto it = types.find(t); it != types.end()) return it->second;
    if (startsWith(t, "texture2d<") && endsWith(t, ">")) return std::string("texture2d");
    if (structs.count(t)) return cppName(t);
    return std::nullopt;
}

/// Metal attributes Metallic understands (anything else in `[[ ]]` is ordinary code).
bool isShaderAttribute(const std::string& inner) {
    if (contains({"position", "flat", "stage_in", "vertex_id", "instance_id", "thread_position_in_grid", "threads_per_grid",
                  "center_perspective", "center_no_perspective"}, inner)) return true;
    for (const std::string f : {"texture", "buffer", "sampler", "user", "color"}) {
        if (startsWith(inner, f + "(") && endsWith(inner, ")")) {
            const std::string arg = inner.substr(f.size() + 1, inner.size() - f.size() - 2);
            return f == "user" || (!arg.empty() && std::all_of(arg.begin(), arg.end(), [](char c) { return c >= '0' && c <= '9'; }));
        }
    }
    return false;
}

/// Joins type tokens back into source text ("texture2d<float>", "[Float]").
std::string joinType(const Strings& ts) {
    std::string out;
    bool lastWasWord = false;
    for (const auto& t : ts) {
        const bool isWord = !t.empty() && identChar(static_cast<unsigned char>(t[0]));
        if (lastWasWord && isWord) out += " ";
        out += t;
        lastWasWord = isWord;
    }
    return out;
}

/// Splits a run of operator characters ("*-") into operators.
std::optional<Strings> splitOperators(const std::string& run) {
    static const Strings ops{"...", "..<", "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&+", "&-",
                             "&*", "<<", ">>", "->", "??", "?.", "+", "-", "*", "/", "%", "<", ">", "=", "!", "&", "|", "^", "~", "?", "."};
    Strings out;
    std::string_view rest = run;
    while (!rest.empty()) {
        const auto it = std::find_if(ops.begin(), ops.end(), [&](const std::string& o) { return startsWith(rest, o); });
        if (it == ops.end()) return std::nullopt;
        out.push_back(*it);
        rest.remove_prefix(it->size());
    }
    return out;
}

Toks shaderTokens(std::string_view src) {
    Toks out;
    for (auto& t : tokenize(src)) {
        if (t.opaque) { out.push_back({t.text, t.line, true}); continue; }
        const bool opRun = t.text.size() > 1 && std::all_of(t.text.begin(), t.text.end(),
                                                             [](char c) { return std::string_view("=+-*/%<>!&|^~?.").find(c) != std::string_view::npos; });
        if (opRun && t.text != "..<" && t.text != "...") {
            if (const auto ops = splitOperators(t.text)) { for (auto& o : *ops) out.push_back({o, t.line, false}); }
            else out.push_back({t.text, t.line, true});
            continue;
        }
        out.push_back({t.text, t.line, false});
    }
    return out;
}

std::optional<std::size_t> matching(const Toks& ts, std::size_t open) {
    const std::string o = ts[open].text, c = o == "(" ? ")" : o == "[" ? "]" : "}";
    int depth = 0;
    for (std::size_t i = open; i < ts.size(); ++i) {
        if (ts[i].text == o) ++depth;
        else if (ts[i].text == c && --depth == 0) return i;
    }
    return std::nullopt;
}

bool startsIdent(const std::string& s) { return !s.empty() && identStart(static_cast<unsigned char>(s[0])); }
bool startsDigit(const std::string& s) { return !s.empty() && s[0] >= '0' && s[0] <= '9'; }

// MARK: - Declarations

std::optional<std::vector<Param>> shaderParams(const Toks& ts, Range r) {
    std::vector<Param> params;
    std::vector<const Tok*> cur;
    auto finish = [&]() -> bool {
        if (cur.empty()) return true;
        Strings idents, type;
        std::string attr;
        bool seenColon = false;
        for (std::size_t m = 0; m < cur.size();) {
            const std::string& t = cur[m]->text;
            if (t == "[" && m + 1 < cur.size() && cur[m + 1]->text == "[") {
                std::size_t j = m + 2;
                std::string inner;
                while (j + 1 < cur.size() && !(cur[j]->text == "]" && cur[j + 1]->text == "]")) inner += cur[j++]->text;
                if (isShaderAttribute(inner)) { attr = inner; m = j + 2; continue; }
            }
            if (!seenColon) { if (t == ":") seenColon = true; else idents.push_back(t); }
            else if (t == "=") break;
            else type.push_back(t);
            ++m;
        }
        if (type.empty() || std::find(type.begin(), type.end(), "inout") != type.end() || std::find(type.begin(), type.end(), "...") != type.end())
            return false;
        Param p;
        if (idents.size() >= 2) p.label = idents[0];
        p.type = joinType(type);
        p.attribute = attr;
        p.name = idents.empty() ? "_" : idents.back();
        params.push_back(p);
        cur.clear();
        return true;
    };
    int depth = 0;
    for (std::size_t k = r.lo; k < r.hi; ++k) {
        const std::string& t = ts[k].text;
        if (t == "(" || t == "[" || t == "<") ++depth;
        if (t == ")" || t == "]" || t == ">") --depth;
        if (t == "," && depth == 0) { if (!finish()) return std::nullopt; }
        else cur.push_back(&ts[k]);
    }
    if (!finish()) return std::nullopt;
    return params;
}

std::optional<Struct> cppStruct(const std::string& name, const Toks& ts, Range r, const Structs& known) {
    Struct st{name, {}, std::nullopt, false};
    std::size_t k = r.lo;
    int depth = 0;
    while (k < r.hi) {
        const std::string& t = ts[k].text;
        if (t == "{") ++depth; else if (t == "}") --depth;
        if (depth == 0 && (t == "func" || t == "init" || t == "subscript")) {
            std::size_t b = k;
            while (b < r.hi && ts[b].text != "{") ++b;
            const auto e = b < r.hi ? matching(ts, b) : std::nullopt;
            if (!e) return std::nullopt;
            k = *e + 1;
            continue;
        }
        if (depth == 0 && t == "static") {
            // static members don't affect layout; skip the line
            const int line = ts[k].line;
            while (k < r.hi && ts[k].line == line) ++k;
            continue;
        }
        if (depth == 0 && (t == "var" || t == "let") && k + 1 < r.hi) {
            std::size_t m = k + 1;
            while (m < r.hi) {
                const std::string fname = ts[m].text;
                if (!(m + 1 < r.hi && ts[m + 1].text == ":")) return std::nullopt;      // stored properties need a type
                m += 2;
                Strings type;
                std::string attr;
                const int line = m < r.hi ? ts[m].line : 0;
                int angle = 0;
                while (m < r.hi && ts[m].line == line) {
                    const std::string& x = ts[m].text;
                    if (x == "<") ++angle; else if (x == ">") --angle;
                    if (angle == 0 && contains({",", ";", "=", "{", "}"}, x)) break;
                    if (x == "[" && m + 1 < r.hi && ts[m + 1].text == "[") {
                        std::size_t j = m + 2;
                        std::string inner;
                        while (j + 1 < r.hi && !(ts[j].text == "]" && ts[j + 1].text == "]")) inner += ts[j++].text;
                        if (isShaderAttribute(inner)) { attr = inner; m = j + 2; continue; }
                    }
                    type.push_back(x);
                    ++m;
                }
                if (m < r.hi && ts[m].text == "{") return std::nullopt;                   // computed property
                const std::string swiftType = joinType(type);
                const auto ct = cppType(swiftType, known);
                if (!ct || *ct == "texture2d" || *ct == "sampler") return std::nullopt;
                st.fields.push_back({fname, *ct, swiftType, attr == "flat"});
                if (attr == "position") st.position = fname;
                if (m < r.hi && ts[m].text == "=") {
                    st.hasDefaults = true;
                    while (m < r.hi && ts[m].line == line && ts[m].text != "," && ts[m].text != ";") ++m;
                }
                if (m < r.hi && ts[m].text == "," && ts[m].line == line) { ++m; continue; }
                break;
            }
            k = m;
            continue;
        }
        ++k;
    }
    return st;
}

std::string structDecl(const Struct& st) {
    const std::string n = cppName(st.name);
    std::string t = "struct " + n + " {\n";
    for (const auto& f : st.fields) t += "    " + f.type + " " + cppName(f.name) + ";\n";
    t += "    static " + n + " interpolate(const " + n + "& a, const " + n + "& b, const " + n + "& c, float3 w) {\n        " + n + " r = a;\n";
    if (st.position)
        for (const auto& f : st.fields)
            if (!f.flat && contains({"float", "float2", "float3", "float4"}, f.type)) {
                const std::string x = cppName(f.name);
                t += "        r." + x + " = a." + x + " * w.x + b." + x + " * w.y + c." + x + " * w.z;\n";
            }
    t += "        (void)b; (void)c; (void)w;\n        return r;\n    }\n};\n\n";
    return t;
}

// MARK: - Bodies

/// Joins C++ tokens with sensible spacing and one statement per line.
std::string render(const Strings& out) {
    std::string text = "    ";
    int indent = 1;
    std::string prev;
    int inFor = 0;
    auto rstrip = [&] { while (!text.empty() && text.back() == ' ') text.pop_back(); };
    auto lastIsIdent = [&](const std::string& s) { return !s.empty() && identChar(static_cast<unsigned char>(s.back())); };
    for (std::size_t k = 0; k < out.size(); ++k) {
        const std::string& t = out[k];
        if (t == "}") {
            indent = std::max(1, indent - 1);
            rstrip();
            text += std::string(static_cast<std::size_t>(indent) * 4, ' ') + "}\n" + std::string(static_cast<std::size_t>(indent) * 4, ' ');
            prev = t;
            continue;
        }
        const bool tight = prev.empty() || prev == "(" || prev == "[" || prev == "." || prev == "!" || t == ")" || t == "]" || t == "," ||
                           t == ";" || t == "." || (t == "(" && lastIsIdent(prev)) ||
                           (t == "[" && (lastIsIdent(prev) || prev == ")" || prev == "]")) ||
                           (prev == "-" && k >= 2 && contains({"(", ",", "=", "return", "*", "/", "+", "-", "<", ">", "?", ":"}, out[k - 2])) ||
                           prev == "++";
        if (!tight && !text.empty() && text.back() != ' ' && text.back() != '\n') text += " ";
        if (t == "for") inFor = 2;
        text += t;
        if (t == ";" && inFor > 0) { --inFor; text += " "; prev = t; continue; }
        if (t == "{") { ++indent; text += "\n" + std::string(static_cast<std::size_t>(indent) * 4, ' '); }
        else if (t == ";") text += "\n" + std::string(static_cast<std::size_t>(indent) * 4, ' ');
        prev = t;
    }
    rstrip();
    return text;
}

struct Translator {
    const Toks& ts;
    const Structs& structs;
    const Funcs& funcs;

    std::optional<std::string> body(Range r, std::set<std::string>& calls, bool returnsValue) const;

    std::optional<std::string> expr(Range r, std::set<std::string>& calls) const {
        const auto b = body(r, calls, false);
        if (!b) return std::nullopt;
        std::string e = trimNewlines(*b);
        while (endsWith(e, ";")) e = trimNewlines(e.substr(0, e.size() - 1));
        if (e.find(';') != std::string::npos) return std::nullopt;
        return e;
    }

    std::optional<std::string> function(const Func& f, std::set<std::string>& calls) const {
        Strings params;
        for (const auto& p : f.params) {
            const auto t = cppType(p.type, structs);
            if (!t) return std::nullopt;
            if (startsWith(p.attribute, "buffer(") && startsWith(p.type, "[")) return std::nullopt;   // arrays stay in M++
            params.push_back((structs.count(p.type) ? "const " + *t + "&" : *t) + " " + cppName(p.name));
        }
        std::string result = "void";
        if (!f.result.empty()) {
            const auto r = cppType(joinType(f.result), structs);
            if (!r) return std::nullopt;
            result = *r;
        }
        const auto b = body(f.body, calls, result != "void");
        if (!b) return std::nullopt;
        return "MTL_INLINE " + result + " " + cppName(f.name) + "(" + join(params, ", ") + ") {\n" + *b + "}\n\n";
    }
};

std::optional<std::string> Translator::body(Range r, std::set<std::string>& calls, bool returnsValue) const {
    Strings out;
    std::size_t i = r.lo;
    int paren = 0;                                   // ( and [ depth
    std::vector<bool> braceInit;                     // per open paren: struct init `T(` -> `T{`
    std::optional<std::string> header;               // "if" / "while": waiting for the block's `{`
    std::optional<std::string> inDecl;               // "let" / "var" while in a declaration statement
    bool atStart = true;
    int blocks = 0;
    int lastLine = r.empty() ? 0 : ts[r.lo].line;
    int statements = 0;
    bool sawReturn = false;
    const auto none = std::optional<std::string>();

    auto endStatement = [&] {
        if (!out.empty() && out.back() != "{" && out.back() != ";" && out.back() != "}") out.push_back(";");
        inDecl.reset();
        atStart = true;
    };
    auto isOperand = [](const std::string& s) { return !s.empty() && (identChar(static_cast<unsigned char>(s.back())) || s == ")" || s == "]"); };
    /// Index in `out` where the postfix expression ending at the end of `out` starts.
    auto operandStart = [&]() -> std::optional<std::size_t> {
        long k = static_cast<long>(out.size()) - 1;
        while (k >= 0) {
            const std::string t = out[static_cast<std::size_t>(k)];
            if (t == ")" || t == "]" || t == "}") {
                const std::string open = t == ")" ? "(" : t == "]" ? "[" : "{";
                int d = 0;
                long j = k;
                for (; j >= 0; --j) {
                    if (out[static_cast<std::size_t>(j)] == t) ++d;
                    else if (out[static_cast<std::size_t>(j)] == open && --d == 0) break;
                }
                if (j < 0) return std::nullopt;
                k = j;
                if (k > 0 && !out[static_cast<std::size_t>(k - 1)].empty() && identChar(static_cast<unsigned char>(out[static_cast<std::size_t>(k - 1)].back()))) --k;
            } else if (t.empty() || !identChar(static_cast<unsigned char>(t.back()))) {
                return std::nullopt;
            }
            if (k > 0 && out[static_cast<std::size_t>(k - 1)] == ".") { k -= 2; continue; }
            return static_cast<std::size_t>(k);
        }
        return std::nullopt;
    };
    auto wrapOperand = [&](const std::string& fn) {
        const auto st = operandStart();
        if (!st) return false;
        out.insert(out.begin() + static_cast<long>(*st), {fn, "("});
        out.push_back(")");
        return true;
    };
    auto text = [&](std::size_t k) -> const std::string& { static const std::string empty; return k < r.hi ? ts[k].text : empty; };
    auto labelAt = [&](std::size_t k) { return k + 1 < r.hi && startsIdent(ts[k].text) && ts[k + 1].text == ":"; };

    while (i < r.hi) {
        const Tok& tk = ts[i];
        const std::string& t = tk.text;
        if (tk.isString) return none;
        // A new line ends a statement unless it continues an expression.
        if (tk.line != lastLine) {
            lastLine = tk.line;
            const bool continues = contains({"+", "-", "*", "/", "%", "&&", "||", "?", ":", ".", "==", "!=", "<", ">", "<=", ">=", "=", ")", "]"}, t);
            if (paren == 0 && !header && !continues && !out.empty() &&
                !contains({"{", ";", "}", "=", "+", "-", "*", "/", "&&", "||", "?", ":", ",", "(", "<", ">", "<=", ">=", "==", "!=", "!"}, out.back()))
                endStatement();
        }
        if (atStart) {
            atStart = false;
            ++statements;
            if (t == "let" || t == "var") {
                inDecl = t;
                if (i + 1 >= r.hi) return none;
                const std::string name = ts[i + 1].text;
                if (!startsIdent(name)) return none;                  // tuples etc.
                std::size_t j = i + 2;
                if (j < r.hi && ts[j].text == ":") {
                    Strings type;
                    ++j;
                    int angle = 0;
                    while (j < r.hi && ts[j].line == tk.line) {
                        const std::string& x = ts[j].text;
                        if (x == "<") ++angle; else if (x == ">") --angle;
                        if (angle == 0 && (x == "=" || x == "," || x == ";")) break;
                        type.push_back(x);
                        ++j;
                    }
                    const auto ct = cppType(joinType(type), structs);
                    if (!ct) return none;
                    if (t == "let") out.push_back("const");
                    out.insert(out.end(), {*ct, cppName(name)});
                    if (!(j < r.hi && ts[j].text == "=")) out.push_back("{}");
                } else {
                    if (t == "let") out.push_back("const");
                    out.insert(out.end(), {"auto", cppName(name)});
                }
                i = j;
                continue;
            }
            if (t == "if" || t == "while") {
                out.insert(out.end(), {t, "("});
                header = t;
                ++i;
                if (contains({"let", "var", "case"}, text(i))) return none;
                continue;
            }
            if (t == "for") {
                // for name in a ..< b {
                if (!(i + 3 < r.hi && ts[i + 2].text == "in")) return none;
                const std::string v = ts[i + 1].text == "_" ? "_mtl_i" + std::to_string(i) : cppName(ts[i + 1].text);
                std::size_t j = i + 3;
                int d = 0;
                std::optional<std::size_t> split;
                bool inclusive = false;
                for (; j < r.hi; ++j) {
                    const std::string& x = ts[j].text;
                    if (x == "(" || x == "[") ++d; else if (x == ")" || x == "]") --d;
                    if (d == 0 && x == "{") break;
                    if (d == 0 && (x == "..<" || x == "...")) { split = j; inclusive = x == "..."; }
                }
                if (!split || j >= r.hi) return none;
                std::set<std::string> c2;
                const auto lo = expr({i + 3, *split}, c2), hi = expr({*split + 1, j}, c2);
                if (!lo || !hi) return none;
                calls.insert(c2.begin(), c2.end());
                out.insert(out.end(), {"for", "(", "long", v, "=", "(", *lo, ")", ";", v, inclusive ? "<=" : "<", "(", *hi, ")", ";", "++", v, ")", "{"});
                ++blocks;
                atStart = true;
                i = j + 1;
                continue;
            }
            if (t == "return") { sawReturn = true; out.push_back("return"); ++i; continue; }
            if (t == "break" || t == "continue") { out.push_back(t); ++i; continue; }
            if (t == "else") { out.push_back("else"); ++i; atStart = true; --statements; continue; }
            if (contains({"guard", "switch", "repeat", "defer", "do", "throw", "try", "func", "struct", "class", "enum", "case", "#if"}, t)) return none;
        }
        if (t == "{") {
            if (header) { out.insert(out.end(), {")", "{"}); header.reset(); }
            else if (!out.empty() && out.back() == "else") out.push_back("{");
            else return none;                                   // a closure or a bare block
            ++blocks;
            atStart = true;
            ++i;
            continue;
        }
        if (t == "}") {
            endStatement();
            if (blocks-- <= 0) return none;
            out.push_back("}");
            atStart = true;
            ++i;
            continue;
        }
        if (t == ";") { endStatement(); ++i; continue; }
        if (t == ",") {
            if (paren == 0) {
                if (header) { out.push_back("&&"); ++i; continue; }          // if a, b {
                if (inDecl) {
                    // let a = 1, b = 2  ->  two declarations
                    const std::string kw = *inDecl;
                    endStatement();
                    std::size_t j = i + 1;
                    const std::string name = text(j);
                    if (!startsIdent(name)) return none;
                    ++j;
                    if (text(j) == ":") return none;
                    if (kw == "let") out.push_back("const");
                    out.insert(out.end(), {"auto", cppName(name)});
                    inDecl = kw;
                    atStart = false;
                    i = j;
                    continue;
                }
                return none;
            }
            out.push_back(",");
            ++i;
            if (labelAt(i)) i += 2;                              // labels: f(x, label: y)
            continue;
        }
        if (t == "(" || t == "[") {
            if (t == "[" && !(!out.empty() && isOperand(out.back()))) return none;   // array literal
            ++paren;
            bool isInit = false;
            if (t == "(" && !out.empty()) {
                const std::string& prev = out.back();
                const std::string base = endsWith(prev, "_") ? prev.substr(0, prev.size() - 1) : prev;
                if ((structs.count(base) || structs.count(prev)) && (out.size() < 2 || out[out.size() - 2] != ".")) {
                    isInit = true;
                    const auto it = structs.count(prev) ? structs.find(prev) : structs.find(base);
                    if (it->second.hasDefaults) return none;
                }
            }
            braceInit.push_back(isInit);
            out.push_back(isInit ? "{" : t);
            ++i;
            if (labelAt(i)) i += 2;
            continue;
        }
        if (t == ")" || t == "]") {
            --paren;
            if (paren < 0 || braceInit.empty()) return none;
            const bool isInit = braceInit.back();
            braceInit.pop_back();
            out.push_back(isInit ? "}" : t);
            ++i;
            continue;
        }
        if (t == ".") {
            if (i + 1 >= r.hi) return none;
            const std::string m = ts[i + 1].text;
            if (!(!out.empty() && isOperand(out.back()))) return none;               // .enumCase
            const std::string next = text(i + 2);
            if ((m == "squareRoot" || m == "sum") && next == "(" && text(i + 3) == ")") {
                if (!wrapOperand(m == "squareRoot" ? "sqrt" : "sum")) return none;
                i += 4;
                continue;
            }
            if (m == "rounded" && next == "(") {
                if (text(i + 3) == ")") { if (!wrapOperand("round")) return none; i += 4; continue; }
                if (text(i + 3) == "." && text(i + 5) == ")") {
                    static const std::map<std::string, std::string> modes{{"down", "floor"}, {"up", "ceil"}, {"towardZero", "trunc"}, {"toNearestOrAwayFromZero", "round"}};
                    const auto it = modes.find(text(i + 4));
                    if (it == modes.end() || !wrapOperand(it->second)) return none;
                    i += 6;
                    continue;
                }
                return none;
            }
            if (m == "isFinite") { if (!wrapOperand("isfinite")) return none; i += 2; continue; }
            if (contains({"magnitude", "squareRoot", "rounded", "map", "reduce", "min", "max", "description", "isNaN", "sign"}, m)) return none;
            if (out.back() == "Float" || out.back() == "float") {
                if (m == "pi") { out.back() = "pi"; i += 2; continue; }
                return none;
            }
            out.insert(out.end(), {".", cppName(m)});
            i += 2;
            continue;
        }
        if (t == "&+" || t == "&-" || t == "&*") { out.push_back(t.substr(1)); ++i; continue; }
        if (t == "%") { out.push_back("%"); ++i; continue; }
        if (contains({"&", "|", "^", "<<", ">>", "~", "..<", "...", "??", "?.", "->"}, t)) return none;   // precedence differs from C (or not C at all)
        if (t == "!") {
            if (!out.empty() && isOperand(out.back())) return none;    // force unwrap
            out.push_back("!");
            ++i;
            continue;
        }
        if (t == "/") {
            // 1 / 2 is 0.5 in a Float context in M++, 0 in C++: keep such code in M++.
            if (!out.empty() && startsDigit(out.back()) && !endsWith(out.back(), "f") && startsDigit(text(i + 1)) && text(i + 1).find('.') == std::string::npos)
                return none;
            out.push_back("/");
            ++i;
            continue;
        }
        // Literals
        if (startsDigit(t)) {
            std::string lit = replaceAll(t, "_", "");
            if (startsWith(lit, "0x") || startsWith(lit, "0b") || startsWith(lit, "0o")) {
                if (startsWith(lit, "0o")) lit = "0" + lit.substr(2);
            } else if (lit.find_first_of(".eE") != std::string::npos) {
                lit += "f";
            }
            out.push_back(lit);
            ++i;
            continue;
        }
        // Identifiers
        if (startsIdent(t)) {
            if (contains({"self", "Self", "nil", "as", "is", "in", "try", "await", "inout", "some", "any", "throws", "if", "switch",
                          "let", "var", "guard", "func", "return", "where", "case", "default", "_"}, t)) return none;
            const std::string next = text(i + 1);
            const bool afterDot = !out.empty() && out.back() == ".";
            if (next == "(" && !afterDot) {
                // This file's own functions come first (they may shadow the built-ins).
                if (const auto it = funcs.find(t); it != funcs.end()) {
                    if (!it->second.kind.empty()) return none;
                    calls.insert(t);
                    out.push_back(cppName(t));
                    ++i;
                    continue;
                }
                if (structs.count(t)) { out.push_back(cppName(t)); ++i; continue; }
                if (const auto it = constructors.find(t); it != constructors.end()) { out.push_back(it->second); ++i; continue; }
                if (shaderMath.count(t)) { out.push_back(t); ++i; continue; }
                return none;
            }
            if (t == "true" || t == "false") { out.push_back(t); ++i; continue; }
            if ((t == "Float" || t == "float") && next == ".") { out.push_back(t); ++i; continue; }
            out.push_back(cppName(t));
            ++i;
            continue;
        }
        if (contains({"+", "-", "*", "=", "+=", "-=", "*=", "/=", "%=", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "?", ":"}, t)) {
            out.push_back(t);
            ++i;
            continue;
        }
        return none;
    }
    if (header || paren != 0 || blocks != 0) return none;
    endStatement();
    // A single-expression body returns its value.
    if (returnsValue && !sawReturn) {
        if (statements != 1) return none;
        out.insert(out.begin(), "return");
    }
    return render(out);
}

/// The extern "C" entry point the engine calls for a kernel or fragment shader.
std::optional<std::pair<std::string, NativeShader>> entry(const Func& f, const std::string& prefix, const Structs& structs) {
    NativeShader info{"mppmtl_" + prefix + "_" + f.name, {}, {}};
    std::string binds;
    Strings args;
    std::string stage = "NoVaryings";
    for (std::size_t i = 0; i < f.params.size(); ++i) {
        const Param& p = f.params[i];
        const std::string& a = p.attribute;
        auto index = [&](const std::string& pre) -> std::optional<int> {
            const std::string s = a.substr(pre.size() + 1, a.size() - pre.size() - 2);
            if (s.empty() || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
            return std::stoi(s);
        };
        const auto t = cppType(p.type, structs);
        if (!t) return std::nullopt;
        const std::string b = "b" + std::to_string(i);
        if (startsWith(a, "buffer(")) {
            const auto n = index("buffer");
            if (!n) return std::nullopt;
            binds += "    const " + *t + "& " + b + " = *static_cast<const " + *t + "*>(B.buffers[" + std::to_string(*n) + "]);\n";
            info.buffers.emplace_back(*n, p.type);
            args.push_back(b);
        } else if (startsWith(a, "texture(")) {
            const auto n = index("texture");
            if (!n) return std::nullopt;
            binds += "    const texture2d " + b + "(B.textures[" + std::to_string(*n) + "]);\n";
            info.textures.push_back(*n);
            args.push_back(b);
        } else if (startsWith(a, "sampler(")) {
            const auto n = index("sampler");
            if (!n) return std::nullopt;
            binds += "    const sampler " + b + "(B.samplers[" + std::to_string(*n) + "]);\n";
            args.push_back(b);
        } else if (a == "thread_position_in_grid") args.push_back("t.threadPosition");
        else if (a == "threads_per_grid") args.push_back("t.threadsPerGrid");
        else if (a == "position") args.push_back("t.position");
        else if (a == "stage_in") {
            if (!structs.count(p.type)) return std::nullopt;
            stage = cppName(p.type);
            args.push_back("in");
        } else return std::nullopt;
    }
    const std::string call = cppName(f.name) + "(" + join(args, ", ") + ")";
    std::string text = "extern \"C\" void " + info.symbol + "(const void* bindings, const void* work) {\n";
    text += "    const mtl_bindings& B = *static_cast<const mtl_bindings*>(bindings);\n    (void)B;\n" + binds;
    if (f.kind == "kernel") {
        text += "    runRows(*static_cast<const mtl_rows*>(work), [&](const thread& t) { (void)t; " + call + "; });\n";
    } else {
        if (f.result.empty()) return std::nullopt;
        text += "    shadeSpan<" + stage + ">(*static_cast<const mtl_span*>(work), [&](const thread& t, const " + stage +
                "& in) { (void)t; (void)in; return " + call + "; });\n";
    }
    text += "}\n\n";
    return std::make_pair(text, info);
}

struct Translation {
    std::string cpp;
    std::map<std::string, NativeShader> shaders;
};

/// Translates a file's kernels and fragment shaders (nullopt when none can be).
std::optional<Translation> translate(std::string_view src, const std::string& prefix) {
    if (src.find("@kernel") == std::string_view::npos && src.find("@fragment") == std::string_view::npos) return std::nullopt;
    const Toks ts = shaderTokens(src);
    Structs structs;
    Strings structOrder;
    Funcs funcs;
    std::vector<std::pair<std::string, Range>> rawStructs;

    int depth = 0;
    std::string pendingKind;
    for (std::size_t i = 0; i < ts.size();) {
        const std::string& t = ts[i].text;
        if (t == "{") { ++depth; ++i; continue; }
        if (t == "}") { --depth; ++i; continue; }
        if (depth == 0 && t == "@" && i + 1 < ts.size() && contains({"kernel", "fragment", "vertex"}, ts[i + 1].text)) {
            pendingKind = ts[i + 1].text;
            i += 2;
            continue;
        }
        if (depth == 0 && t == "struct" && i + 1 < ts.size()) {
            const std::string name = ts[i + 1].text;
            std::size_t b = i + 2;
            while (b < ts.size() && ts[b].text != "{") ++b;
            const auto e = b < ts.size() ? matching(ts, b) : std::nullopt;
            if (!e) break;
            rawStructs.push_back({name, {b + 1, *e}});
            i = *e + 1;
            pendingKind.clear();
            continue;
        }
        if (depth == 0 && t == "func" && i + 1 < ts.size()) {
            const std::string name = ts[i + 1].text;
            std::size_t p = i + 2;
            while (p < ts.size() && ts[p].text != "(" && ts[p].text != "{") ++p;
            const auto pe = p < ts.size() && ts[p].text == "(" ? matching(ts, p) : std::nullopt;
            if (!pe) { ++i; continue; }
            std::size_t b = *pe + 1;
            Strings result;
            if (b < ts.size() && ts[b].text == "->") {
                ++b;
                while (b < ts.size() && ts[b].text != "{" && ts[b].text != "where") result.push_back(ts[b++].text);
            }
            while (b < ts.size() && ts[b].text != "{") ++b;
            const auto be = b < ts.size() ? matching(ts, b) : std::nullopt;
            if (!be) { ++i; continue; }
            if (const auto params = shaderParams(ts, {p + 1, *pe})) {
                if (funcs.count(name)) funcs[name] = Func{name, "overloaded", {}, {}, {}};
                else funcs[name] = Func{name, pendingKind, *params, result, {b + 1, *be}};
            }
            pendingKind.clear();
            i = *be + 1;
            continue;
        }
        ++i;
    }

    // Structs whose stored properties all have C++ types (a few passes for nesting).
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& [name, range] : rawStructs)
            if (!structs.count(name))
                if (auto st = cppStruct(name, ts, range, structs)) { structs[name] = *st; structOrder.push_back(name); }

    const Translator tr{ts, structs, funcs};
    std::map<std::string, std::string> helperText;
    Strings helperOrder;
    std::set<std::string> failedHelpers;
    std::function<bool(const std::string&, std::set<std::string>)> helper = [&](const std::string& name, std::set<std::string> stack) {
        if (helperText.count(name)) return true;
        if (failedHelpers.count(name) || stack.count(name)) return false;
        const auto it = funcs.find(name);
        if (it == funcs.end() || !it->second.kind.empty()) { failedHelpers.insert(name); return false; }
        std::set<std::string> calls;
        const auto text = tr.function(it->second, calls);
        if (!text) { failedHelpers.insert(name); return false; }
        stack.insert(name);
        for (const auto& c : calls) if (!helper(c, stack)) { failedHelpers.insert(name); return false; }
        helperText[name] = *text;
        helperOrder.push_back(name);
        return true;
    };

    Translation out;
    std::string entries, shaderText;
    for (const auto& [name, f] : funcs) {
        if (f.kind != "kernel" && f.kind != "fragment") continue;
        std::set<std::string> calls;
        const auto text = tr.function(f, calls);
        bool ok = text.has_value();
        if (ok) for (const auto& c : calls) if (!helper(c, {})) { ok = false; break; }
        const auto e = ok ? entry(f, prefix, structs) : std::nullopt;
        if (!e) {
            if (env("MPP_SHADER_DEBUG")) {
                Strings names;
                for (const auto& [n, _] : structs) names.push_back(n);
                writeStderr("shader " + name + ": stays in M++ (structs: " + join(names, ", ") + "; body " + (text ? "translated" : "not translatable") + ")\n");
            }
            continue;
        }
        shaderText += *text;
        entries += e->first;
        out.shaders[name] = e->second;
    }
    if (out.shaders.empty()) return std::nullopt;

    std::string cpp = "// Generated by mpp from Metallic shaders: do not edit.\n#include \"MetallicShader.hpp\"\n\n";
    cpp += "namespace mtl { namespace shaders_" + prefix + " {\n\n";
    for (const auto& name : structOrder) cpp += structDecl(structs[name]);
    for (const auto& h : helperOrder) cpp += trim(helperText[h].substr(0, helperText[h].find('{'))) + ";\n";
    cpp += "\n";
    for (const auto& h : helperOrder) cpp += helperText[h];
    cpp += shaderText + entries + "}}  // namespace\n";
    out.cpp = cpp;
    return out;
}

std::string shadersJSON(const std::map<std::string, NativeShader>& shaders) {
    std::string s = "{";
    bool first = true;
    for (const auto& [name, sh] : shaders) {
        if (!first) s += ",";
        first = false;
        s += jsonString(name) + ":{\"symbol\":" + jsonString(sh.symbol) + ",\"buffers\":[";
        for (std::size_t k = 0; k < sh.buffers.size(); ++k)
            s += (k ? "," : "") + std::string("[") + std::to_string(sh.buffers[k].first) + "," + jsonString(sh.buffers[k].second) + "]";
        s += "],\"textures\":[";
        for (std::size_t k = 0; k < sh.textures.size(); ++k) s += (k ? "," : "") + std::to_string(sh.textures[k]);
        s += "]}";
    }
    return s + "}";
}

}  // namespace

// MARK: - Building shaders and cores

/// For each source with shaders: write the C++, compile it with MppxxCpp's
/// flags (optimized, with its usual checks), and return what to link. A file
/// whose C++ doesn't compile keeps all its shaders in M++.
static std::string prepareShaders(const Strings& sources, const std::string& module, const std::string& outDir, bool release) {
    if (env("MPP_METALLIC_NATIVE").value_or("") == "0") return "null";
    std::string natives = "{";
    Strings objects, inputs;
    bool any = false;
    for (const auto& src : sources) {
        if (path::ext(src) != ".mpp") continue;
        const std::string text = read(src);
        const std::string prefix = moduleName(module + "_" + path::stem(src));
        const auto t = translate(text, prefix);
        if (!t) continue;
        if (env("MPP_SHADER_DEBUG")) writeStderr(t->cpp);
        makeDirectories(outDir);
        const std::string cpp = path::join(outDir, prefix + ".cpp");
        writeIfChanged(cpp, t->cpp);
        const std::string core = path::join(config().home, "MppLang/mpp-metallic/Core");
        Strings flags = compileFlags("c++23", false, true);
        flags.insert(flags.end(), {"-I", core, "-O3", "-fno-math-errno", "-Wno-unused-parameter", "-Wno-unused-variable"});
        if (!release) flags.push_back("-g");
        const std::string obj = path::join(outDir, prefix + (release ? "" : "-debug") + ".o");
        try {
            for (auto& d : compile(cpp, obj, flags, path::stem(src) + " shaders (C++23)", true)) inputs.push_back(d);
            objects.push_back(obj);
            natives += std::string(any ? "," : "") + jsonString(src) + ":" + shadersJSON(t->shaders);
            any = true;
        } catch (const Error& e) {
            if (verbose() || env("MPP_SHADER_DEBUG")) writeStderr("note: " + path::display(src) + ": Metallic shaders stay in M++ (" + e.message + ")\n");
        }
        inputs.push_back(src);
    }
    if (!any) return "null";
    return "{\"natives\":" + natives + "},\"objects\":" + jsonArray(objects) + ",\"inputs\":" + jsonArray(inputs) + "}";
}

/// A standard module's C++23 core, compiled with MppxxCpp's flags into
/// lib<Name>Core.a, with a module map for its C header.
static std::string cxxCore(const std::string& name, const std::string& dir, const std::string& outDir, bool release) {
    const std::string out = path::join(path::join(outDir, "cxx"), name);
    makeDirectories(path::join(out, "obj"));
    const std::string header = path::join(dir, name + "Core.h");
    if (!isFile(header)) throw Error{name + ": missing C++ core header " + header};
    Strings flags = compileFlags("c++23", false, release);
    flags.insert(flags.end(), {"-I", dir});
    if (release) flags.insert(flags.end(), {"-O3", "-fno-math-errno"});
    Strings objects, inputs{header};
    for (const auto& f : list(dir)) {
        if (!isSource(f)) continue;
        const std::string obj = path::join(path::join(out, "obj"), path::stem(f) + ".o");
        for (auto& d : compile(path::join(dir, f), obj, flags, name + " core (C++23)" + (release ? " (release)" : ""))) if (d != header) inputs.push_back(d);
        objects.push_back(obj);
    }
    const std::string lib = path::join(outDir, "lib" + name + "Core.a");
    archive(objects, lib);
    writeIfChanged(path::join(out, "module.modulemap"), "module " + name + "Core {\n    header \"" + header + "\"\n    export *\n}\n");
    return "{\"library\":" + jsonString(lib) + ",\"mapDir\":" + jsonString(out) + ",\"inputs\":" + jsonArray(inputs) + "}";
}

}  // namespace mppxx

using namespace mppxx;

extern "C" {

char* mppxx_shaders_translate(const char* text, const char* prefix) {
    const auto t = translate(text ? text : "", prefix ? prefix : "");
    if (!t) return toC("null");
    return toC("{\"cpp\":" + jsonString(t->cpp) + ",\"shaders\":" + shadersJSON(t->shaders) + "}");
}

char* mppxx_shaders_prepare(const char* sources, const char* module, const char* outDir, int release) {
    try { return toC(prepareShaders(split(sources ? sources : "", '\n'), module, outDir, release != 0)); }
    catch (const Error& e) { return errorJSON(e.message); }
}

char* mppxx_cxx_core(const char* name, const char* dir, const char* outDir, int release) {
    try { return toC(cxxCore(name, dir, outDir, release != 0)); }
    catch (const Error& e) { return errorJSON(e.message); }
}

}  // extern "C"
