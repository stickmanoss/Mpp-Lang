// MppxxCpp — core: Swift-style names, fatal errors, checked conversions.
//
// Part of MppxxCpp (`#include <mpp.hpp>`). Compiled by `mpp file.cpp`, every
// signed overflow, out-of-bounds access to a fixed array, division by zero
// and failed standard-library precondition stops the program with a message,
// as in M++. Nothing here turns a check off.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unistd.h>

#if __cplusplus < 201703L
#error "MppxxCpp needs ISO C++17 or later (mpp compiles C++20 by default)"
#endif

namespace mpp {

// MARK: Swift-style type names

using Int = std::ptrdiff_t;     // Swift's Int (word-sized; imported into M++ as Int)
using Int8 = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;
using UInt = std::uintptr_t;    // Swift's UInt
using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
using Double = double;
using Float = float;
using Bool = bool;
using String = std::string;
using Substring = std::string_view;

// MARK: Fatal errors

namespace detail {
/// Set once a fatal error has been reported, so the runtime's trap handler
/// doesn't print a second message for the same failure.
inline std::atomic<bool> fatalReported{false};
/// Flushes buffered standard output before the program stops (set by io.hpp).
inline void (*flushOnFatal)() = nullptr;

inline void writeAll(int fd, const char* p, std::size_t n) noexcept {
    while (n > 0) {
        ssize_t w = ::write(fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
}

inline void writeNumber(int fd, long v) noexcept {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    bool neg = v < 0;
    unsigned long u = neg ? 0UL - static_cast<unsigned long>(v) : static_cast<unsigned long>(v);
    do { buf[--i] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
    if (neg) buf[--i] = '-';
    writeAll(fd, buf + i, static_cast<std::size_t>(23 - i));
}
} // namespace detail

} // namespace mpp

// For the MppxxCpp runtime (written in M++), which finds these by name when
// the program starts: flush buffered output, and claim the right to report
// a fatal error (so a failure is described once).
extern "C" {
__attribute__((used, visibility("default"))) inline void mpp_cxx_flush_output() noexcept {
    if (::mpp::detail::flushOnFatal) ::mpp::detail::flushOnFatal();
}
__attribute__((used, visibility("default"))) inline int mpp_cxx_claim_fatal() noexcept {
    return ::mpp::detail::fatalReported.exchange(true) ? 0 : 1;
}
}

namespace mpp {

/// Stops the program, printing `file:line: Fatal error: message` like M++.
[[noreturn]] inline void fatalError(std::string_view message = {}, const char* file = __builtin_FILE(),
                                    int line = __builtin_LINE()) noexcept {
    if (detail::flushOnFatal) detail::flushOnFatal();
    detail::fatalReported.store(true);
    if (line > 0) {
        detail::writeAll(2, file, std::char_traits<char>::length(file));
        detail::writeAll(2, ":", 1);
        detail::writeNumber(2, line);
        detail::writeAll(2, ": ", 2);
    }
    detail::writeAll(2, "Fatal error", 11);
    if (!message.empty()) {
        detail::writeAll(2, ": ", 2);
        detail::writeAll(2, message.data(), message.size());
    }
    detail::writeAll(2, "\n", 1);
    __builtin_trap();
}

/// `precondition(cond, "message")`: always checked, in every build.
inline void precondition(bool condition, std::string_view message = {}, const char* file = __builtin_FILE(),
                         int line = __builtin_LINE()) noexcept {
    if (__builtin_expect(!condition, 0)) fatalError(message.empty() ? "Precondition failed" : message, file, line);
}

/// Like `precondition`; kept as a separate name for M++ code ported to C++.
inline void require(bool condition, std::string_view message = {}, const char* file = __builtin_FILE(),
                    int line = __builtin_LINE()) noexcept {
    precondition(condition, message, file, line);
}

// MARK: Optionals

/// `x!` — the value, or a fatal error naming the call site.
template <class T>
T& unwrap(std::optional<T>& o, std::string_view message = "Unexpectedly found nil while unwrapping an Optional value",
          const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    if (!o) fatalError(message, file, line);
    return *o;
}
template <class T>
const T& unwrap(const std::optional<T>& o,
                std::string_view message = "Unexpectedly found nil while unwrapping an Optional value",
                const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    if (!o) fatalError(message, file, line);
    return *o;
}
template <class T>
T unwrap(std::optional<T>&& o, std::string_view message = "Unexpectedly found nil while unwrapping an Optional value",
         const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    if (!o) fatalError(message, file, line);
    return std::move(*o);
}

// MARK: Integers

/// Converts between integer types, stopping if the value doesn't fit
/// (Swift's `Int32(x)`). Use `truncating<To>(x)` to keep the low bits instead.
template <class To, class From>
constexpr To narrow(From v, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>, "narrow converts between integer types");
    To r = static_cast<To>(v);
    if (static_cast<From>(r) != v || ((r < To{}) != (v < From{}))) fatalError("Integer value doesn't fit in the target type", file, line);
    return r;
}

/// Swift's `Int32(truncatingIfNeeded: x)`.
template <class To, class From>
constexpr To truncating(From v) noexcept {
    return static_cast<To>(static_cast<std::make_unsigned_t<From>>(v));
}

/// Overflow-checked arithmetic with a message (mpp-compiled code also traps
/// on plain signed overflow; these say what happened and where).
template <class T>
constexpr T add(T a, T b, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    T r;
    if (__builtin_add_overflow(a, b, &r)) fatalError("Arithmetic overflow", file, line);
    return r;
}
template <class T>
constexpr T subtract(T a, T b, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    T r;
    if (__builtin_sub_overflow(a, b, &r)) fatalError("Arithmetic overflow", file, line);
    return r;
}
template <class T>
constexpr T multiply(T a, T b, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    T r;
    if (__builtin_mul_overflow(a, b, &r)) fatalError("Arithmetic overflow", file, line);
    return r;
}

/// Swift's `&+`, `&-`, `&*`: wrapping arithmetic, on purpose.
template <class T>
constexpr T wrappingAdd(T a, T b) noexcept {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) + static_cast<U>(b));
}
template <class T>
constexpr T wrappingSubtract(T a, T b) noexcept {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) - static_cast<U>(b));
}
template <class T>
constexpr T wrappingMultiply(T a, T b) noexcept {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) * static_cast<U>(b));
}

// MARK: Ranges

/// `for (Int i : range(0, n))` — Swift's `0..<n`.
struct Range {
    Int lowerBound, upperBound;
    struct Iterator {
        Int i;
        constexpr Int operator*() const noexcept { return i; }
        constexpr Iterator& operator++() noexcept { ++i; return *this; }
        constexpr bool operator!=(const Iterator& o) const noexcept { return i != o.i; }
    };
    constexpr Iterator begin() const noexcept { return {lowerBound}; }
    constexpr Iterator end() const noexcept { return {upperBound}; }
    constexpr Int count() const noexcept { return upperBound - lowerBound; }
    constexpr bool contains(Int v) const noexcept { return v >= lowerBound && v < upperBound; }
};

/// `range(a, b)` is `a..<b`; it stops if `b < a`, as Swift does.
constexpr Range range(Int lower, Int upper, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    if (upper < lower) fatalError("Range requires lowerBound <= upperBound", file, line);
    return {lower, upper};
}
constexpr Range range(Int upper, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    return range(0, upper, file, line);
}
/// `through(a, b)` is `a...b`.
constexpr Range through(Int lower, Int upper, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
    return range(lower, add<Int>(upper, 1, file, line), file, line);
}

// MARK: defer

template <class F>
class Defer {
public:
    explicit Defer(F f) noexcept : f_(std::move(f)) {}
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    ~Defer() { f_(); }

private:
    F f_;
};

namespace detail {
struct DeferMaker {
    template <class F>
    Defer<F> operator+(F f) const noexcept { return Defer<F>(std::move(f)); }
};
} // namespace detail

} // namespace mpp

#define MPP_CONCAT_(a, b) a##b
#define MPP_CONCAT(a, b) MPP_CONCAT_(a, b)
/// `MPP_DEFER { close(fd); };` — runs when the scope ends, like Swift's `defer`.
#define MPP_DEFER auto MPP_CONCAT(mpp_defer_, __LINE__) = ::mpp::detail::DeferMaker{} + [&]()
