// MppxxCpp wrapper — what the parts share: the compiler, flags, compiles.
#pragma once

#include "support.hpp"

namespace mppxx {

inline const Strings standards{"c++17", "c++20", "c++23", "c++26"};
inline constexpr const char* defaultStandard = "c++23";

bool isSource(std::string_view path);
const std::string& compiler();                 // throws Error
bool isClang();
std::string home();                            // MppLang/mpp-cpp
std::string includeDir();
Strings compileFlags(const std::string& standard, bool strict, bool release);
/// Compiles one translation unit unless its object is newer than everything
/// it was built from; returns the files it depends on.
Strings compile(const std::string& src, const std::string& obj, const Strings& flags, const std::string& show, bool quietErrors = false);
void archive(const Strings& objects, const std::string& lib);
std::string runtimeLibrary(bool release);
Strings runtimeLinkFlagsSwiftc(const std::string& lib);
Strings swiftcCxxLinkFlags();
Strings swiftcLinkFlags(const Strings& flags);   // -Wl,a,b -> -Xlinker a -Xlinker b
std::string cxxStdlib();
Strings platformTarget();                       // -target … on macOS

struct Directives {
    std::optional<std::string> standard;
    bool strict = false;
    Strings sources, compile, link;
};
Directives directives(const std::string& text, const std::string& dir);

}  // namespace mppxx
