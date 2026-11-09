// The MppxxCpp wrapper, in C++23: everything `mpp` does with C++.
//
//   • building C++ programs (`mpp hello.cpp`): the compiler, MppxxCpp's
//     safety flags, `// mpp:` directives, cached compiles, the runtime, linking;
//   • M++ calling C++ (`import cpp "x.hpp"`, `#cpp { }` blocks);
//   • Metallic: translating shaders to C++ and compiling them, and the C++
//     cores of standard modules.
//
// The M++ driver links this library and calls it through this C interface.
// Strings returned as `char*` are JSON (or plain text where noted), malloc'd:
// free them with mppxx_free. A JSON object with an "error" key is a failure.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Where M++ lives, its cache, the swiftc it uses (and a key identifying
/// that toolchain), and the running `mpp` (its changes invalidate caches).
void mppxx_configure(const char *home, const char *cache, const char *swiftc, const char *toolchainKey, const char *selfExe);
void mppxx_free(char *p);

// MARK: C++ programs

/// 1 for .cpp/.cc/.cxx/.c++/.C files.
int mppxx_is_cpp_source(const char *path);
/// 1 for a known standard ("c++17" … "c++26").
int mppxx_is_standard(const char *standard);
/// Builds (cached) the program `source`: {"exe": path} or {"error": ...}.
/// standard/strict: from the command line (NULL / -1: the file's own, else the default).
char *mppxx_cpp_program(const char *source, int release, const char *standard, int strict);
/// The C++ compiler, for `mpp --version` (plain text).
char *mppxx_cpp_describe(void);
/// "-lc++" on macOS, "-lstdc++" elsewhere (static string).
const char *mppxx_cxx_stdlib(void);
/// Archives objects (newline-separated) into lib unless it is newer: {} or {"error"}.
char *mppxx_archive(const char *objects, const char *lib);
/// A Make-style .d file's prerequisites: JSON array.
char *mppxx_parse_dep_file(const char *text);

// MARK: M++ calling C++

/// `import cpp "x.hpp"` lines and `#cpp { }` blocks, replaced by imports
/// (same line count): {"text", "headers": [{"path","module","line"}], "inline"}.
char *mppxx_interop_extract(const char *text, const char *file);
/// Module maps and compiled C++ for an M++ program: null (no C++),
/// {"swiftFlags", "linkFlags", "inputs"} or {"error"}.
char *mppxx_interop_prepare(const char *source, const char *text, const char *outDir, int release, const char *standard, int strict);
/// Swift added to programs that call C++ (static string).
const char *mppxx_interop_bridge_source(void);

// MARK: Metallic

/// A file's @kernel/@fragment shaders in C++: null, or {"cpp", "shaders":
/// {name: {"symbol", "buffers": [[index, "SwiftType"]], "textures": [index]}}}.
char *mppxx_shaders_translate(const char *text, const char *prefix);
/// Translates and compiles the shaders of `sources` (newline-separated):
/// null, or {"natives": {source: shaders}, "objects", "inputs"}.
char *mppxx_shaders_prepare(const char *sources, const char *module, const char *outDir, int release);
/// A standard module's C++23 core (<name>Core.h plus .cpp files in dir):
/// {"library", "mapDir", "inputs"} or {"error"}.
char *mppxx_cxx_core(const char *name, const char *dir, const char *outDir, int release);

#ifdef __cplusplus
}
#endif
