// MppxxCpp wrapper — calling C++ from M++.
//
//   import cpp "fastmath.hpp"      // a C++ header next to this file; a
//                                  // fastmath.cpp beside it is compiled too
//   #cpp {                         // C++ written inside the M++ file
//       inline mpp::Int triple(mpp::Int x) { return 3 * x; }
//   }
//   print(triple(14), fastmath.mean([1, 2, 3]))
//
// Headers become Clang modules that M++ imports through Swift's C++
// interoperability; C++ sources are compiled with MppxxCpp's flags (C++23,
// or C++26 when asked) and linked in. Errors in a `#cpp` block point at the
// M++ file's lines.

#include "mppxx.h"
#include "source.hpp"
#include "wrapper.hpp"

#include <algorithm>
#include <unistd.h>

namespace mppxx {

struct HeaderImport { std::string path, module; int line; };
struct Extraction {
    std::string text;                 // the M++ source with C++ removed (same line count)
    std::vector<HeaderImport> headers;
    std::string inlineCode;           // C++ from `#cpp { }` blocks, with #line markers
};

static bool mentionsCpp(std::string_view t) { return t.find("#cpp") != std::string_view::npos || t.find("import cpp") != std::string_view::npos; }

/// The `}` matching the C++ `{` at `open`, skipping C++ comments, strings,
/// raw strings, character literals and digit separators.
static std::size_t matchCppBrace(std::string_view b, std::size_t open) {
    const std::size_t n = b.size();
    auto isDigitContext = [&](std::size_t i) {                 // 1'000'000
        std::size_t j = i;
        while (j > 0 && identChar(static_cast<unsigned char>(b[j - 1]))) --j;
        return j < i && b[j] >= '0' && b[j] <= '9';
    };
    int depth = 0;
    std::size_t i = open;
    while (i < n) {
        const char c = b[i];
        if (c == '/' && i + 1 < n && b[i + 1] == '/') { while (i < n && b[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && b[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(b[i] == '*' && b[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (c == 'R' && i + 1 < n && b[i + 1] == '"' &&
            (i == 0 || !identChar(static_cast<unsigned char>(b[i - 1])) || b[i - 1] == 'u' || b[i - 1] == '8' || b[i - 1] == 'L' || b[i - 1] == 'U')) {
            std::size_t j = i + 2;
            std::string delim;
            while (j < n && b[j] != '(') delim += b[j++];
            const std::string end = ")" + delim + "\"";
            const std::size_t at = b.find(end, j + 1);
            i = at == std::string_view::npos ? n : at + end.size();
            continue;
        }
        if (c == '"' || (c == '\'' && !(i > 0 && identChar(static_cast<unsigned char>(b[i - 1])) && isDigitContext(i)))) {
            ++i;
            while (i < n && b[i] != c && b[i] != '\n') { if (b[i] == '\\') ++i; ++i; }
            ++i;
            continue;
        }
        if (c == '{') ++depth;
        if (c == '}' && --depth == 0) return i;
        ++i;
    }
    return std::string_view::npos;
}

static Extraction extract(const std::string& source, const std::string& file) {
    Extraction result{source, {}, ""};
    if (!mentionsCpp(source)) return result;
    std::string bytes = source;
    const std::string inlineName = moduleName(path::stem(file)) + "_cpp";
    bool importedInline = false;
    std::size_t searchFrom = 0;
    auto lineAt = [&](std::size_t pos) { return 1 + static_cast<int>(std::count(bytes.begin(), bytes.begin() + static_cast<long>(pos), '\n')); };
    while (true) {
        const std::vector<bool> mask = codeMask(bytes);
        // The next `#cpp` or `import cpp` in code.
        std::size_t start = std::string::npos;
        bool block = false;
        for (std::size_t i = searchFrom; i < bytes.size(); ++i) {
            if (!mask[i]) continue;
            if (bytes.compare(i, 4, "#cpp") == 0 && (i + 4 == bytes.size() || !identChar(static_cast<unsigned char>(bytes[i + 4])))) {
                start = i; block = true; break;
            }
            if (bytes[i] == 'i' && (i == 0 || bytes[i - 1] == '\n' || bytes[i - 1] == ' ' || bytes[i - 1] == '\t') &&
                bytes.compare(i, 10, "import cpp") == 0 && i + 10 < bytes.size() && (bytes[i + 10] == ' ' || bytes[i + 10] == '\t')) {
                start = i; block = false; break;
            }
        }
        if (start == std::string::npos) break;
        const int line = lineAt(start);
        if (!block) {
            // import cpp "path"  ->  import <module>
            std::size_t e = bytes.find('\n', start);
            if (e == std::string::npos) e = bytes.size();
            const Strings parts = split(bytes.substr(start, e - start), '"', true);
            if (parts.size() < 3 || parts[1].empty()) { searchFrom = e; continue; }
            const std::string module = moduleName(path::stem(parts[1]));
            result.headers.push_back({parts[1], module, line});
            const std::string replacement = "import " + module;
            bytes.replace(start, e - start, replacement);
            searchFrom = start + replacement.size();
            continue;
        }
        // #cpp { ... }
        std::size_t open = start + 4;
        while (open < bytes.size() && (bytes[open] == ' ' || bytes[open] == '\t' || bytes[open] == '\n' || bytes[open] == '\r')) ++open;
        const std::size_t close = open < bytes.size() && bytes[open] == '{' ? matchCppBrace(bytes, open) : std::string::npos;
        if (close == std::string::npos) { searchFrom = start + 4; continue; }
        result.inlineCode += "#line " + std::to_string(lineAt(open)) + " \"" + file + "\"\n" + bytes.substr(open + 1, close - open - 1) + "\n";
        // Same number of lines: the import, then blank lines.
        const auto newlines = std::count(bytes.begin() + static_cast<long>(start), bytes.begin() + static_cast<long>(close) + 1, '\n');
        std::string replacement = importedInline ? "" : "import " + inlineName;
        replacement.append(static_cast<std::size_t>(newlines), '\n');
        importedInline = true;
        bytes.replace(start, close - start + 1, replacement);
        searchFrom = start + replacement.size();
    }
    result.text = bytes;
    return result;
}

static const char* bridgeSource = R"(import CxxStdlib

extension String {
    /// A C++ `std::string` as an M++ `String`.
    init(cpp s: std.string) { self.init(cString: s.__c_strUnsafe()) }
}
)";

/// Some Linux toolchains ship without the C++-interop support libraries
/// (libswiftCxx, libswiftCxxStdlib). Programs that only call C++ functions
/// and types don't need any code from them, so empty stand-ins let the link
/// succeed. Returns "" when the real ones exist.
static std::string interopStubDir() {
#if defined(__APPLE__)
    return "";
#else
    const Config& cfg = config();
    const std::string probe = path::join(path::join(cfg.cache, "toolchains"), stamp("cxxlibs|" + cfg.toolchainKey).substr(0, 24));
    if (const auto known = readIfExists(probe)) return *known == "present" ? "" : *known;
    std::string resource;
    try {
        for (const auto& line : split(run({cfg.swiftc, "-print-target-info"}).output, '\n')) {
            if (line.find("\"runtimeResourcePath\"") == std::string::npos) continue;
            const Strings parts = split(line, '"', true);
            if (parts.size() >= 4) resource = parts[3];
        }
    } catch (const Error&) {}
    for (const char* d : {"swift/linux", "swift_static/linux", "swift/linux/x86_64", "swift/linux/aarch64"})
        for (const char* lib : {"libswiftCxx.a", "libswiftCxx.so"})
            if (isFile(path::join(path::join(path::dirname(resource), d), lib))) { write(probe, "present"); return ""; }
    const std::string dir = path::join(cfg.cache, "cxx-interop-stubs");
    makeDirectories(dir);
    const std::string c = path::join(dir, "empty.c"), obj = path::join(dir, "empty.o");
    write(c, "/* intentionally empty: stands in for a missing Swift C++ interop library */\n");
    if (run({compiler(), "-x", "c", "-c", c, "-o", obj}).status != 0) throw Error{"could not build C++ interop stand-ins"};
    for (const char* lib : {"libswiftCxx.a", "libswiftCxxStdlib.a"}) {
        const std::string l = path::join(dir, lib);
        ::unlink(l.c_str());
        const RunResult a = run({which("ar").empty() ? "ar" : which("ar"), "rcs", l, obj});
        if (a.status != 0) throw Error{"could not archive C++ interop stand-ins:\n" + a.output};
    }
    write(probe, dir);
    return dir;
#endif
}

/// Writes the module map and headers for `source`'s C++, compiles the C++
/// sources beside its headers, and returns the flags for swiftc.
static std::string prepare(const std::string& source, const std::string& text, const std::string& outDir, bool release,
                           const std::optional<std::string>& cliStandard, bool strict) {
    if (!mentionsCpp(text)) return "null";
    const Extraction ex = extract(text, source);
    if (ex.headers.empty() && ex.inlineCode.empty()) return "null";
    const std::string dir = path::dirname(source), cppDir = path::join(outDir, "cpp");
    makeDirectories(cppDir);
    // The wrapper is C++23 (or newer, with --std=c++26). Swift 6's importer can't
    // read the C++23 standard library headers yet (constexpr unique_ptr meets
    // incomplete types in <chrono>), so it reads headers as C++20; the C++ itself
    // is compiled, and checked, as C++23.
    const std::string standard = cliStandard && *cliStandard == "c++26" ? "c++26" : "c++23";
    const std::string importStandard = "c++20";
    Strings swiftFlags, linkFlags, inputs;
    std::string modulemap;
    Strings cxxFlags = compileFlags(standard, strict, release);
    cxxFlags.push_back("-DMPP_SHARED_STDOUT=1");

    for (const auto& h : ex.headers) {
        const std::string p = startsWith(h.path, "/") ? h.path : path::join(dir, h.path);
        if (!isFile(p)) throw Error{path::display(source) + ":" + std::to_string(h.line) + ": `import cpp \"" + h.path + "\"`: header not found: " + p};
        modulemap += "module " + h.module + " {\n    header \"" + p + "\"\n    requires cplusplus\n    export *\n}\n";
        inputs.push_back(p);
        // fastmath.hpp -> fastmath.cpp (or .cc/.cxx) beside it
        const std::string base = path::join(path::dirname(p), path::stem(p));
        for (const char* e : {".cpp", ".cc", ".cxx"}) {
            const std::string impl = base + e;
            if (!isFile(impl)) continue;
            const Directives d = directives(read(impl), path::dirname(impl));
            Strings all{impl};
            all.insert(all.end(), d.sources.begin(), d.sources.end());
            for (const auto& src : all) {
                const std::string obj = path::join(path::join(cppDir, "obj"), moduleName(path::stem(src)) + ".o");
                Strings f = cxxFlags;
                f.insert(f.end(), d.compile.begin(), d.compile.end());
                for (auto& dep : compile(src, obj, f, path::display(src) + (release ? " (release)" : ""))) inputs.push_back(dep);
                linkFlags.push_back(obj);
            }
            for (auto& l : swiftcLinkFlags(d.link)) linkFlags.push_back(l);
            break;
        }
    }
    if (!ex.inlineCode.empty()) {
        const std::string name = moduleName(path::stem(source)) + "_cpp";
        const std::string header = path::join(cppDir, name + ".hpp");
        writeIfChanged(header, "#pragma once\n#include <mpp.hpp>\n" + ex.inlineCode);
        // Check the C++ with clang first: its errors honor the #line markers, so they point at the M++ file.
        Strings cmd{compiler()};
        cmd.insert(cmd.end(), cxxFlags.begin(), cxxFlags.end());
        cmd.insert(cmd.end(), {"-Wno-pragma-once-outside-header", "-fsyntax-only", "-x", "c++", header});
        const RunResult check = run(cmd);
        const std::string said = trimNewlines(check.output);
        if (!said.empty()) writeStderr(said + "\n");
        if (check.status != 0) throw Error{"the C++ in " + path::display(source) + " failed to compile"};
        modulemap += "module " + name + " {\n    header \"" + header + "\"\n    requires cplusplus\n    export *\n}\n";
        inputs.push_back(header);
    }
    writeIfChanged(path::join(cppDir, "module.modulemap"), modulemap);

    swiftFlags = {"-cxx-interoperability-mode=default", "-I", cppDir, "-Xcc", "-std=" + importStandard, "-Xcc", "-I" + includeDir(),
                  "-Xcc", "-DMPP_HARDENED=1", "-Xcc", "-DMPP_SHARED_STDOUT=1", "-Xcc", "-D_GLIBCXX_ASSERTIONS",
                  "-Xcc", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST"};
    const std::string runtime = runtimeLibrary(release);
    for (auto& f : runtimeLinkFlagsSwiftc(runtime)) linkFlags.push_back(f);
    linkFlags.push_back(cxxStdlib());
    for (auto& f : swiftcCxxLinkFlags()) linkFlags.push_back(f);
    inputs.push_back(runtime);
    if (const std::string stub = interopStubDir(); !stub.empty()) { swiftFlags.push_back("-L"); swiftFlags.push_back(stub); }
    inputs.push_back(path::join(includeDir(), "mpp.hpp"));
    for (const auto& f : list(path::join(includeDir(), "mpp"))) inputs.push_back(path::join(path::join(includeDir(), "mpp"), f));
    return "{\"swiftFlags\":" + jsonArray(swiftFlags) + ",\"linkFlags\":" + jsonArray(linkFlags) + ",\"inputs\":" + jsonArray(inputs) + "}";
}

}  // namespace mppxx

using namespace mppxx;

extern "C" {

char* mppxx_interop_extract(const char* text, const char* file) {
    const Extraction ex = extract(text ? text : "", file ? file : "");
    std::string headers = "[";
    for (std::size_t i = 0; i < ex.headers.size(); ++i) {
        if (i) headers += ",";
        headers += "{\"path\":" + jsonString(ex.headers[i].path) + ",\"module\":" + jsonString(ex.headers[i].module) +
                   ",\"line\":" + std::to_string(ex.headers[i].line) + "}";
    }
    headers += "]";
    return toC("{\"text\":" + jsonString(ex.text) + ",\"headers\":" + headers + ",\"inline\":" + jsonString(ex.inlineCode) + "}");
}

char* mppxx_interop_prepare(const char* source, const char* text, const char* outDir, int release, const char* standard, int strict) {
    try {
        const std::optional<std::string> s = standard ? std::optional<std::string>(standard) : std::nullopt;
        return toC(prepare(source, text, outDir, release != 0, s, strict > 0));
    } catch (const Error& e) { return errorJSON(e.message); }
}

const char* mppxx_interop_bridge_source(void) { return bridgeSource; }

}  // extern "C"
