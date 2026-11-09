// MppxxCpp — ISO control: which C++ standard this file is compiled as, and
// whether GNU extensions, strict ISO mode and M++'s hardening are on.
//
// The standard is chosen with `mpp --std=c++17|20|23|26 file.cpp` or a
// `// mpp: --std=c++23` line at the top of the file (default: ISO C++20,
// no GNU extensions). `--iso-strict` turns every ISO-conformance warning
// into an error.
#pragma once

#include <string_view>

namespace mpp::iso {

/// The ISO C++ standard year: 17, 20, 23 or 26.
constexpr int standard() noexcept {
#if __cplusplus > 202302L
    return 26;
#elif __cplusplus >= 202302L
    return 23;
#elif __cplusplus >= 202002L
    return 20;
#elif __cplusplus >= 201703L
    return 17;
#else
    return 14;
#endif
}

/// "C++20" and so on.
constexpr std::string_view name() noexcept {
    switch (standard()) {
    case 26: return "C++26";
    case 23: return "C++23";
    case 20: return "C++20";
    case 17: return "C++17";
    default: return "C++14";
    }
}

/// The raw value of `__cplusplus`.
constexpr long version = __cplusplus;

/// True when compiled as ISO C++ without GNU extensions (`-std=c++NN`).
#if defined(__STRICT_ANSI__)
constexpr bool pure = true;
#else
constexpr bool pure = false;
#endif

/// True under `mpp --iso-strict` (conformance warnings are errors).
#if defined(MPP_ISO_STRICT)
constexpr bool strict = true;
#else
constexpr bool strict = false;
#endif

/// True when compiled by `mpp` with its safety checks (always, through mpp).
#if defined(MPP_HARDENED)
constexpr bool hardened = true;
#else
constexpr bool hardened = false;
#endif

/// The compiler that built this file.
constexpr std::string_view compiler() noexcept {
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "unknown";
#endif
}

} // namespace mpp::iso

/// `MPP_REQUIRE_ISO(23);` — a clear error when the file is built with an
/// older standard than it needs.
#define MPP_REQUIRE_ISO(n) \
    static_assert(::mpp::iso::standard() >= n, "this file needs ISO C++" #n " or later: run `mpp --std=c++" #n "` or add `// mpp: --std=c++" #n "` at the top")
