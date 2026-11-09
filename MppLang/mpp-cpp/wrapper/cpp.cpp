// MppxxCpp wrapper — compiling C++ through mpp.
//
//   mpp hello.cpp                      build (cached) and run
//   mpp --std=c++26 hello.cpp          choose the ISO standard (17, 20, 23, 26; default 23)
//   mpp --iso-strict hello.cpp         ISO-conformance warnings become errors
//
// The compiler is the clang that ships with the Swift toolchain M++ already
// uses (Apple's on macOS), so C++ and M++ share one LLVM. Every build keeps
// M++'s safety: bounds, overflow and division checks trap with a message,
// the C++ standard library checks its preconditions, locals start zeroed.
//
// A file can carry its own options in `// mpp:` lines:
//   // mpp: --std=c++26 --iso-strict
//   // mpp: util.cpp -lz -DNDEBUG_LOGS

#include "mppxx.h"
#include "wrapper.hpp"

#include <algorithm>
#include <unistd.h>

namespace mppxx {

bool isSource(std::string_view p) {
    const std::string e = path::ext(p);
    return e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" || e == ".C";
}

// MARK: - Compiler

/// $MPP_CXX, else the Swift toolchain's clang++, else clang++ / c++ / g++ on PATH.
static std::string findCompiler() {
    if (const auto c = env("MPP_CXX")) {
        const std::string p = which(*c);
        if (p.empty()) throw Error{"MPP_CXX=" + *c + " not found"};
        return p;
    }
    const Config& cfg = config();
    if (!cfg.swiftc.empty()) {
        const std::string cached = path::join(path::join(cfg.cache, "toolchains"), stamp("cxx|" + cfg.toolchainKey).substr(0, 24));
        if (const auto p = readIfExists(cached); p && isFile(*p)) return *p;
        std::string found;
#if defined(__APPLE__)
        // The xcrun shim runs the selected Xcode's clang (the one swiftc uses) and supplies the macOS SDK.
        if (isFile("/usr/bin/clang++")) found = "/usr/bin/clang++";
#endif
        if (found.empty()) {
            try {
                const RunResult r = run({cfg.swiftc, "-print-target-info"});
                for (const auto& line : split(r.output, '\n')) {
                    if (line.find("\"runtimeResourcePath\"") == std::string::npos) continue;
                    const Strings parts = split(line, '"', true);
                    if (parts.size() >= 4) {
                        // ".../usr/lib/swift" -> ".../bin/clang++"
                        const std::string p = path::join(path::dirname(path::dirname(parts[3])), "bin/clang++");
                        if (isFile(p)) found = p;
                    }
                }
            } catch (const Error&) {}
        }
        if (!found.empty()) {
            try { write(cached, found); } catch (const Error&) {}
            return found;
        }
    }
    for (const char* c : {"clang++", "c++", "g++"}) if (auto p = which(c); !p.empty()) return p;
    throw Error{"no C++ compiler found (install clang, or set MPP_CXX)"};
}

const std::string& compiler() {
    static const std::string c = findCompiler();
    return c;
}

bool isClang() {
    static const bool clang = [] {
        try {
            if (path::basename(compiler()).find("clang") != std::string::npos) return true;
            return run({compiler(), "--version"}).output.find("clang") != std::string::npos;
        } catch (const Error&) { return false; }
    }();
    return clang;
}

std::string home() { return path::join(config().home, "MppLang/mpp-cpp"); }
std::string includeDir() { return path::join(home(), "include"); }

[[maybe_unused]] static std::string macosTarget() { return env("MPP_MACOS_TARGET").value_or("14.0"); }

Strings platformTarget() {
#if defined(__APPLE__)
#if defined(__arm64__) || defined(__aarch64__)
    return {"-target", "arm64-apple-macos" + macosTarget()};
#else
    return {"-target", "x86_64-apple-macos" + macosTarget()};
#endif
#else
    return {};
#endif
}

// MARK: - Flags

Strings compileFlags(const std::string& standard, bool strict, bool release) {
    Strings f{"-std=" + standard, "-I", includeDir(), "-DMPP_HARDENED=1"};
    for (auto& t : platformTarget()) f.push_back(t);
    if (release) f.insert(f.end(), {"-O2", "-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=2"});
    else f.insert(f.end(), {"-O0", "-g"});
    f.insert(f.end(), {"-fno-omit-frame-pointer", "-fstack-protector-strong", "-ftrivial-auto-var-init=zero"});
    // The standard library checks indices and preconditions (libstdc++ and libc++).
    f.insert(f.end(), {"-D_GLIBCXX_ASSERTIONS", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST"});
    // Undefined behavior that M++ rules out becomes a trap (no runtime library).
    f.push_back("-fsanitize=bounds,signed-integer-overflow,integer-divide-by-zero,shift,vla-bound,return,unreachable,builtin");
    f.push_back(isClang() ? "-fsanitize-trap=all" : "-fsanitize-undefined-trap-on-error");
    f.insert(f.end(), {"-Wall", "-Wextra", "-pedantic", "-Werror=return-type"});
    if (strict) f.insert(f.end(), {"-pedantic-errors", "-Werror", "-DMPP_ISO_STRICT=1"});
    if (stderrIsTTY()) f.push_back(isClang() ? "-fcolor-diagnostics" : "-fdiagnostics-color=always");
    return f;
}

/// Linking a C++ program with the C++ compiler (no Swift runtime).
static Strings cxxLinkFlags() {
#if defined(__APPLE__)
    return {};
#else
    return {"-ldl", "-rdynamic"};
#endif
}

Strings swiftcCxxLinkFlags() {
#if defined(__APPLE__)
    return {};
#else
    return {"-ldl", "-Xlinker", "--export-dynamic"};
#endif
}

std::string cxxStdlib() {
#if defined(__APPLE__)
    return "-lc++";
#else
    return "-lstdc++";
#endif
}

Strings swiftcLinkFlags(const Strings& flags) {
    Strings out;
    for (const auto& f : flags) {
        if (startsWith(f, "-Wl,")) for (const auto& part : split(f.substr(4), ',')) out.insert(out.end(), {"-Xlinker", part});
        else out.push_back(f);
    }
    return out;
}

// MARK: - Directives

Directives directives(const std::string& text, const std::string& dir) {
    Directives d;
    for (const auto& raw : split(text, '\n')) {
        const std::string line = trim(raw);
        if (!startsWith(line, "// mpp:")) continue;
        const Strings words = split(line.substr(7), ' ');
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::string& w = words[i];
            if (startsWith(w, "--std=") || startsWith(w, "-std=")) {
                const std::string s = w.substr(w.find('=') + 1);
                if (std::find(standards.begin(), standards.end(), s) == standards.end())
                    throw Error{"unknown C++ standard `" + s + "` in `// mpp:` (use one of " + join(standards, ", ") + ")"};
                d.standard = s;
            } else if (w == "--iso-strict") {
                d.strict = true;
            } else if (w == "-framework" && i + 1 < words.size()) {
                d.link.insert(d.link.end(), {w, words[i + 1]});
                ++i;
            } else if (startsWith(w, "-l") || startsWith(w, "-L") || startsWith(w, "-Wl,")) {
                d.link.push_back(w);
            } else if (startsWith(w, "-D") || startsWith(w, "-U")) {
                d.compile.push_back(w);
            } else if (startsWith(w, "-I")) {
                const std::string p = w.substr(2);
                d.compile.push_back("-I" + (startsWith(p, "/") ? p : path::join(dir, p)));
            } else if (isSource(w) || path::ext(w) == ".c") {
                const std::string p = startsWith(w, "/") ? w : path::join(dir, w);
                if (!isFile(p)) throw Error{"`// mpp: " + w + "`: file not found: " + p};
                d.sources.push_back(p);
            } else {
                throw Error{"unknown `// mpp:` option `" + w + "` (allowed: --std=c++NN, --iso-strict, other .cpp files, -l/-L/-framework, -D/-U/-I)"};
            }
        }
    }
    return d;
}

// MARK: - Compiling

static Strings parseDepFile(const std::string& text) {
    std::string joined = replaceAll(replaceAll(text, "\\\n", " "), "\\ ", "\x01");
    Strings out;
    bool first = true;
    std::string word;
    auto flush = [&] {
        if (word.empty()) return;
        if (first) { first = false; if (endsWith(word, ":")) { word.clear(); return; } }
        if (!endsWith(word, ":")) out.push_back(replaceAll(word, "\x01", " "));
        word.clear();
    };
    for (const char c : joined) {
        if (c == ' ' || c == '\n' || c == '\t') flush();
        else word += c;
    }
    flush();
    return out;
}

Strings compile(const std::string& src, const std::string& obj, const Strings& flags, const std::string& show, bool quietErrors) {
    const std::string& cxx = compiler();
    const std::string rec = obj + ".rec";
    Strings keyParts{cxx};
    keyParts.insert(keyParts.end(), flags.begin(), flags.end());
    const std::string key = stamp(join(keyParts, " "));
    if (const auto r = readIfExists(rec); r && isFile(obj)) {
        const Strings lines = split(*r, '\n');
        if (!lines.empty() && lines[0] == key) {
            bool ok = true;
            Strings deps;
            for (std::size_t k = 1; k < lines.size() && ok; ++k) {
                const Strings f = split(lines[k], '\t');
                const auto i = f.size() == 3 ? info(f[0]) : std::nullopt;
                ok = i && std::to_string(i->mtimeNanos) == f[1] && std::to_string(i->size) == f[2];
                deps.push_back(f.empty() ? "" : f[0]);
            }
            if (ok) return deps;
        }
    }
    status("Compiling", show);
    makeDirectories(path::dirname(obj));
    const std::string depFile = obj + ".d";
    Strings cmd{cxx};
    cmd.insert(cmd.end(), flags.begin(), flags.end());
    cmd.insert(cmd.end(), {"-MD", "-MF", depFile, "-c", src, "-o", obj});
    if (verbose()) writeStderr("+ " + join(cmd, " ") + "\n");
    const RunResult r = run(cmd);
    const std::string out = trimNewlines(r.output);
    if (quietErrors) {
        if (r.status != 0) throw Error{"could not compile " + show + ":\n" + out};
    } else {
        if (!out.empty()) writeStderr(out + "\n");
        if (r.status != 0) throw Error{"could not compile " + show};
    }
    const Strings deps = parseDepFile(readIfExists(depFile).value_or(""));
    std::string record = key + "\n";
    for (const auto& p : deps) {
        const auto i = info(p);
        record += p + "\t" + std::to_string(i ? i->mtimeNanos : -1) + "\t" + std::to_string(i ? i->size : -1) + "\n";
    }
    write(rec, record);
    return deps;
}

void archive(const Strings& objects, const std::string& lib) {
    long long newest = 0;
    for (const auto& o : objects) if (const auto i = info(o)) newest = std::max(newest, i->mtimeNanos);
    if (const auto i = info(lib); i && i->mtimeNanos >= newest) return;
    ::unlink(lib.c_str());
    Strings cmd{which("ar").empty() ? "ar" : which("ar"), "rcs", lib};
    cmd.insert(cmd.end(), objects.begin(), objects.end());
    const RunResult r = run(cmd);
    if (r.status != 0) throw Error{"could not archive " + path::display(lib) + ":\n" + r.output};
}

// MARK: - The runtime

/// The MppxxCpp runtime (crash messages, output flushing), C++23
/// (runtime/runtime.cpp), built once into a static library.
std::string runtimeLibrary(bool release) {
    const std::string src = path::join(home(), "runtime/runtime.cpp");
    const std::string dir = path::join(path::join(config().cache, "cpp-runtime"), release ? "release" : "debug");
    const std::string obj = path::join(dir, "runtime.o"), lib = path::join(dir, "libmppcxxrt.a");
    compile(src, obj, compileFlags("c++23", false, release), std::string("MppxxCpp runtime") + (release ? " (release)" : ""));
    archive({obj}, lib);
    return lib;
}

/// Keeps the whole runtime library (nothing calls into it: it installs itself at start-up).
Strings runtimeLinkFlagsSwiftc(const std::string& lib) {
#if defined(__APPLE__)
    return {"-Xlinker", "-force_load", "-Xlinker", lib};
#else
    return {"-Xlinker", "--whole-archive", "-Xlinker", lib, "-Xlinker", "--no-whole-archive"};
#endif
}
static Strings runtimeLinkFlagsCxx(const std::string& lib) {
#if defined(__APPLE__)
    return {"-Wl,-force_load," + lib};
#else
    return {"-Wl,--whole-archive", lib, "-Wl,--no-whole-archive"};
#endif
}

// MARK: - Programs

static bool fastPathValid(const std::string& record, std::string& exe) {
    const Strings lines = split(record, '\n');
    if (lines.empty()) return false;
    const Strings head = split(lines[0], '\t', true);
    if (head.size() != 3 || head[0] != "v1" || head[2] != env("MPP_MACOS_TARGET").value_or("")) return false;
    if (!isFile(head[1])) return false;
    for (std::size_t k = 1; k < lines.size(); ++k) {
        const Strings f = split(lines[k], '\t');
        if (f.size() != 3) return false;
        const auto i = info(f[0]);
        if (!i || std::to_string(i->mtimeNanos) != f[1] || std::to_string(i->size) != f[2]) return false;
    }
    exe = head[1];
    return true;
}

static std::string program(const std::string& source, bool release, const std::optional<std::string>& cliStandard, int cliStrict) {
    const std::string abs = path::absolute(source);
    const std::string text = read(abs);
    const Directives d = directives(text, path::dirname(abs));
    const std::string standard = cliStandard ? *cliStandard : d.standard.value_or(defaultStandard);
    const bool strict = cliStrict >= 0 ? cliStrict != 0 : d.strict;
    const std::string level = std::string(release ? "release" : "debug") + "-" + standard + (strict ? "-strict" : "");
    const std::string out = path::join(path::join(path::join(config().cache, "cpp"), stamp(abs).substr(0, 16)), level);
    const std::string fastFile = path::join(out, "fast");
    if (const auto fast = readIfExists(fastFile)) {
        std::string exe;
        if (fastPathValid(*fast, exe)) return exe;
    }

    Strings flags = compileFlags(standard, strict, release);
    flags.insert(flags.end(), d.compile.begin(), d.compile.end());
    const std::string rt = runtimeLibrary(release);
    Strings objects, inputs{abs};
    std::vector<std::string> used;
    Strings all{abs};
    all.insert(all.end(), d.sources.begin(), d.sources.end());
    for (const auto& src : all) {
        std::string name = path::stem(src);
        while (std::find(used.begin(), used.end(), name) != used.end()) name += "_";
        used.push_back(name);
        const std::string obj = path::join(path::join(out, "obj"), name + ".o");
        Strings f = flags;
        if (path::ext(src) == ".c") {
            for (auto& x : f) if (startsWith(x, "-std=")) x = "-std=c17";
            f.insert(f.end(), {"-x", "c"});
        }
        for (auto& dep : compile(src, obj, f, path::display(src) + (release ? " (release)" : ""))) inputs.push_back(dep);
        objects.push_back(obj);
    }
    const std::string exe = path::join(path::join(out, "bin"), moduleName(path::stem(abs)));
    makeDirectories(path::dirname(exe));
    Strings keyParts = objects;
    keyParts.push_back(rt);
    keyParts.insert(keyParts.end(), d.link.begin(), d.link.end());
    for (auto& f : cxxLinkFlags()) keyParts.push_back(f);
    const std::string linkKey = stamp(join(keyParts, " "));
    long long newest = 0;
    for (const auto& o : objects) if (const auto i = info(o)) newest = std::max(newest, i->mtimeNanos);
    if (const auto i = info(rt)) newest = std::max(newest, i->mtimeNanos);
    const auto exeInfo = info(exe);
    if (!exeInfo || exeInfo->mtimeNanos < newest || readIfExists(exe + ".link").value_or("") != linkKey) {
        status("Linking", path::stem(abs));
        Strings cmd{compiler()};
        if (!release) cmd.push_back("-g");
        cmd.insert(cmd.end(), objects.begin(), objects.end());
        for (auto& f : runtimeLinkFlagsCxx(rt)) cmd.push_back(f);
        cmd.insert(cmd.end(), d.link.begin(), d.link.end());
        for (auto& f : cxxLinkFlags()) cmd.push_back(f);
        for (auto& f : platformTarget()) cmd.push_back(f);
        cmd.insert(cmd.end(), {"-o", exe});
        if (verbose()) writeStderr("+ " + join(cmd, " ") + "\n");
        const RunResult r = run(cmd);
        const std::string o = trimNewlines(r.output);
        if (!o.empty()) writeStderr(o + "\n");
        if (r.status != 0) throw Error{"could not link " + path::display(abs)};
        write(exe + ".link", linkKey);
    }

    std::string record = "v1\t" + exe + "\t" + env("MPP_MACOS_TARGET").value_or("") + "\n";
    Strings seen;
    Strings everything = inputs;
    everything.insert(everything.end(), {rt, compiler(), config().swiftc, config().selfExe});
    for (const auto& p : everything) {
        if (p.empty() || std::find(seen.begin(), seen.end(), p) != seen.end()) continue;
        seen.push_back(p);
        const auto i = info(p);
        record += p + "\t" + std::to_string(i ? i->mtimeNanos : -1) + "\t" + std::to_string(i ? i->size : -1) + "\n";
    }
    write(fastFile, record);
    return exe;
}

}  // namespace mppxx

// MARK: - C interface

using namespace mppxx;

extern "C" {

void mppxx_configure(const char* home, const char* cache, const char* swiftc, const char* toolchainKey, const char* selfExe) {
    Config& c = config();
    c.home = home ? home : "";
    c.cache = cache ? cache : "";
    c.swiftc = swiftc ? swiftc : "";
    c.toolchainKey = toolchainKey ? toolchainKey : "";
    c.selfExe = selfExe ? selfExe : "";
}

void mppxx_free(char* p) { std::free(p); }

int mppxx_is_cpp_source(const char* p) { return p && isSource(p) ? 1 : 0; }

int mppxx_is_standard(const char* s) {
    return s && std::find(standards.begin(), standards.end(), std::string(s)) != standards.end() ? 1 : 0;
}

char* mppxx_cpp_program(const char* source, int release, const char* standard, int strict) {
    try {
        const std::optional<std::string> s = standard ? std::optional<std::string>(standard) : std::nullopt;
        return toC("{\"exe\":" + jsonString(program(source, release != 0, s, strict)) + "}");
    } catch (const Error& e) { return errorJSON(e.message); }
}

char* mppxx_cpp_describe(void) {
    try {
        const std::string& c = compiler();
        const Strings lines = split(run({c, "--version"}).output, '\n');
        std::string v = lines.empty() ? c : lines[0];
        const bool fromSwift = c.find("libexec/swift") != std::string::npos || c.find(".xctoolchain") != std::string::npos
            || (!config().swiftc.empty() && startsWith(c, path::dirname(path::dirname(config().swiftc))));
        return toC(v + (fromSwift ? " (Swift toolchain)" : ""));
    } catch (const Error&) { return toC("not found"); }
}

const char* mppxx_cxx_stdlib(void) {
    static const std::string s = cxxStdlib();
    return s.c_str();
}

char* mppxx_archive(const char* objects, const char* lib) {
    try {
        archive(split(objects ? objects : "", '\n'), lib);
        return toC("{}");
    } catch (const Error& e) { return errorJSON(e.message); }
}

char* mppxx_parse_dep_file(const char* text) { return toC(jsonArray(parseDepFile(text ? text : ""))); }

}  // extern "C"
