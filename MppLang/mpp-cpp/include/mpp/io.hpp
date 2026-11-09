// MppxxCpp — I/O: buffered print and input on raw file descriptors, mapped
// files and atomic writes. The same design as M++'s own I/O: one 64 KiB
// buffer per stream, numbers formatted with `to_chars`, no locales, no
// iostream synchronization, and a `write(2)` only when the buffer fills.
#pragma once

#include "core.hpp"
#include "memory.hpp"

#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <vector>

namespace mpp {

/// An I/O failure: the path and the system's reason.
class IOError : public std::runtime_error {
public:
    IOError(const std::string& what, int code) : std::runtime_error(what), code_(code) {}
    int code() const noexcept { return code_; }

private:
    int code_;
};

namespace detail {
[[noreturn]] inline void throwIO(const std::string& action, const std::string& path, int err) {
    throw IOError(action + " " + path + ": " + std::system_category().message(err), err);
}
} // namespace detail

// MARK: Output

/// A buffered output stream on a file descriptor. Thread-safe: each `print`
/// call is written as a unit.
class Output {
public:
    explicit Output(int fd, bool lineBuffered = false) noexcept : fd_(fd), lineBuffered_(lineBuffered) {}
    /// Writes through a C stdio stream, in order with everything else
    /// written to it (used when M++ and C++ share standard output).
    explicit Output(std::FILE* file) noexcept : fd_(-1), lineBuffered_(true), file_(file) {}
    /// Collects into a string instead of a file descriptor.
    explicit Output(std::string* into) noexcept : fd_(-1), lineBuffered_(false), into_(into) {}
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output() { flush(); }

    void write(const char* p, std::size_t n) noexcept {
        if (n >= sizeof(buf_)) {
            flushUnlocked();
            if (into_) into_->append(p, n);
            else if (file_) std::fwrite(p, 1, n, file_);
            else detail::writeAll(fd_, p, n);
            return;
        }
        if (n_ + n > sizeof(buf_)) flushUnlocked();
        std::memcpy(buf_ + n_, p, n);
        n_ += n;
    }
    void write(std::string_view s) noexcept { write(s.data(), s.size()); }
    void put(char c) noexcept {
        if (n_ == sizeof(buf_)) flushUnlocked();
        buf_[n_++] = c;
    }
    /// Room for `n` bytes written directly (n <= 64).
    char* reserve(std::size_t n) noexcept {
        if (n_ + n > sizeof(buf_)) flushUnlocked();
        return buf_ + n_;
    }
    void commit(std::size_t n) noexcept { n_ += n; }

    void flush() noexcept {
        lock();
        flushUnlocked();
        unlock();
    }
    /// For signal handlers: no lock (async-signal-safe).
    void flushUnlocked() noexcept {
        if (n_) {
            if (into_) into_->append(buf_, n_);
            else if (file_) std::fwrite(buf_, 1, n_, file_);
            else detail::writeAll(fd_, buf_, n_);
        }
        n_ = 0;
    }

    void lock() noexcept {
        while (busy_.test_and_set(std::memory_order_acquire)) {}
    }
    void unlock() noexcept {
        if (lineBuffered_) flushUnlocked();
        busy_.clear(std::memory_order_release);
    }
    int fd() const noexcept { return fd_; }

private:
    int fd_;
    bool lineBuffered_;
    std::string* into_ = nullptr;
    std::FILE* file_ = nullptr;
    std::size_t n_ = 0;
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    char buf_[64 * 1024];
};

/// Standard output (flushed when the program exits, when it reads input,
/// and before a fatal error) and standard error (written at once).
#if defined(MPP_SHARED_STDOUT)
inline Output out{stdout};   // M++ programs calling C++: one stream, one order
#else
inline Output out{1};
#endif
inline Output err{2, true};

namespace detail {
inline const bool installFlush = [] {
    flushOnFatal = [] {
        out.flushUnlocked();
#if defined(MPP_SHARED_STDOUT)
        std::fflush(stdout);
#endif
    };
    return true;
}();

template <class T, class = void>
struct HasDescription : std::false_type {};
template <class T>
struct HasDescription<T, std::void_t<decltype(std::declval<const T&>().description())>> : std::true_type {};

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};
template <class T>
struct IsSequence : std::false_type {};
template <class T>
struct IsSequence<std::vector<T>> : std::true_type {};
template <class T>
struct IsSequence<Array<T>> : std::true_type {};
template <class T>
struct IsSequence<Span<T>> : std::true_type {};

inline void putDouble(Output& o, double v) noexcept {
    if (v != v) { o.write("nan", 3); return; }
    if (v == std::numeric_limits<double>::infinity()) { o.write("inf", 3); return; }
    if (v == -std::numeric_limits<double>::infinity()) { o.write("-inf", 4); return; }
    char* p = o.reserve(40);
    auto r = std::to_chars(p, p + 38, v);
    std::size_t n = static_cast<std::size_t>(r.ptr - p);
    bool plain = true;
    for (std::size_t i = 0; i < n; ++i) if (p[i] == '.' || p[i] == 'e') { plain = false; break; }
    if (plain) { p[n++] = '.'; p[n++] = '0'; }        // Swift prints 3.0, not 3
    o.commit(n);
}
inline void putFloat(Output& o, float v) noexcept {
    if (v != v) { o.write("nan", 3); return; }
    char* p = o.reserve(40);
    auto r = std::to_chars(p, p + 38, v);
    std::size_t n = static_cast<std::size_t>(r.ptr - p);
    bool plain = true;
    for (std::size_t i = 0; i < n; ++i) if (p[i] == '.' || p[i] == 'e' || p[i] == 'n') { plain = false; break; }
    if (plain) { p[n++] = '.'; p[n++] = '0'; }
    o.commit(n);
}

template <class T>
void putValue(Output& o, const T& v, bool quoted);

template <class Seq>
void putSequence(Output& o, const Seq& s) {
    o.put('[');
    bool first = true;
    for (const auto& x : s) {
        if (!first) o.write(", ", 2);
        first = false;
        putValue(o, x, true);
    }
    o.put(']');
}

template <class T>
void putValue(Output& o, const T& v, bool quoted) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        if (v) o.write("true", 4); else o.write("false", 5);
    } else if constexpr (std::is_same_v<D, char>) {
        if (quoted) o.put('"');
        o.put(v);
        if (quoted) o.put('"');
    } else if constexpr (std::is_integral_v<D>) {
        char* p = o.reserve(24);
        auto r = std::to_chars(p, p + 24, v);
        o.commit(static_cast<std::size_t>(r.ptr - p));
    } else if constexpr (std::is_same_v<D, double> || std::is_same_v<D, long double>) {
        putDouble(o, static_cast<double>(v));
    } else if constexpr (std::is_same_v<D, float>) {
        putFloat(o, v);
    } else if constexpr (std::is_convertible_v<const D&, std::string_view>) {
        std::string_view s = v;
        if (quoted) o.put('"');
        o.write(s);
        if (quoted) o.put('"');
    } else if constexpr (std::is_same_v<D, std::nullopt_t>) {
        o.write("nil", 3);
    } else if constexpr (IsOptional<D>::value) {
        if (!v) { o.write("nil", 3); return; }
        o.write("Optional(", 9);
        putValue(o, *v, true);
        o.put(')');
    } else if constexpr (IsSequence<D>::value) {
        putSequence(o, v);
    } else if constexpr (HasDescription<D>::value) {
        putValue(o, v.description(), false);
    } else if constexpr (std::is_pointer_v<D>) {
        char* p = o.reserve(24);
        p[0] = '0';
        p[1] = 'x';
        auto r = std::to_chars(p + 2, p + 24, reinterpret_cast<std::uintptr_t>(v), 16);
        o.commit(static_cast<std::size_t>(r.ptr - p));
    } else {
        static_assert(HasDescription<D>::value,
                      "mpp::print: give this type a `std::string description() const` to print it");
    }
}
} // namespace detail

/// Writes the values with no separator and no newline.
template <class... A>
void write(Output& o, const A&... a) {
    o.lock();
    (detail::putValue(o, a, false), ...);
    o.unlock();
}

/// Swift's `print(a, b, c)`: values separated by spaces, then a newline.
template <class... A>
void print(const A&... a) {
    out.lock();
    bool first = true;
    ((first ? (void)(first = false) : out.put(' '), detail::putValue(out, a, false)), ...);
    out.put('\n');
    out.unlock();
}

/// `printTo(err, ...)`: `print` to another stream.
template <class... A>
void printTo(Output& o, const A&... a) {
    o.lock();
    bool first = true;
    ((first ? (void)(first = false) : o.put(' '), detail::putValue(o, a, false)), ...);
    o.put('\n');
    o.unlock();
}

/// `describe(x)`: the text `print` would show.
template <class T>
std::string describe(const T& v) {
    std::string s;
    Output o(&s);
    detail::putValue(o, v, false);
    o.flushUnlocked();
    return s;
}

// MARK: Input

/// A buffered reader on a file descriptor with a fast scanner for numbers
/// and words. Reading from standard input flushes standard output first.
class Input {
public:
    explicit Input(int fd) noexcept : fd_(fd) {}
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    /// The next line without its newline, or nothing at end of input.
    std::optional<std::string> readLine() {
        std::string s;
        if (!readLine(s)) return std::nullopt;
        return s;
    }
    /// Reads a line into `line` (reusing its storage); false at end of input.
    bool readLine(std::string& line) {
        line.clear();
        bool any = false;
        for (;;) {
            if (pos_ == len_ && !fill()) return any;
            any = true;
            const char* start = buf_ + pos_;
            const char* nl = static_cast<const char*>(std::memchr(start, '\n', len_ - pos_));
            if (nl) {
                line.append(start, static_cast<std::size_t>(nl - start));
                pos_ = static_cast<std::size_t>(nl - buf_) + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            line.append(start, len_ - pos_);
            pos_ = len_;
        }
    }

    /// The next whitespace-separated word, or nothing at end of input.
    std::optional<std::string> readWord() {
        if (!skipSpace()) return std::nullopt;
        std::string s;
        for (;;) {
            std::size_t i = pos_;
            while (i < len_ && !isSpace(buf_[i])) ++i;
            s.append(buf_ + pos_, i - pos_);
            pos_ = i;
            if (pos_ < len_ || !fill()) return s;
        }
    }

    /// The next integer, or nothing at end of input (stops on a malformed number).
    std::optional<Int> readInt(const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        if (!skipSpace()) return std::nullopt;
        if (len_ - pos_ < 32) compact();
        Int v = 0;
        auto r = std::from_chars(buf_ + pos_, buf_ + len_, v);
        if (r.ec == std::errc::result_out_of_range) fatalError("Integer in input is out of range", file, line);
        if (r.ec != std::errc()) fatalError("Expected an integer in input", file, line);
        pos_ = static_cast<std::size_t>(r.ptr - buf_);
        return v;
    }
    /// The next number, or nothing at end of input.
    std::optional<Double> readDouble(const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        auto w = readWord();
        if (!w) return std::nullopt;
        char* end = nullptr;
        double v = std::strtod(w->c_str(), &end);
        if (end != w->c_str() + w->size()) fatalError("Expected a number in input", file, line);
        return v;
    }

    /// Everything that is left.
    std::string readAll() {
        std::string s(buf_ + pos_, len_ - pos_);
        pos_ = len_;
        while (fill()) {
            s.append(buf_, len_);
            pos_ = len_;
        }
        return s;
    }

private:
    int fd_;
    std::size_t pos_ = 0, len_ = 0;
    bool eof_ = false;
    char buf_[64 * 1024];

    static bool isSpace(char c) noexcept { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
    bool fill() {
        if (eof_) return false;
        if (fd_ == 0) out.flush();
        ssize_t n;
        do { n = ::read(fd_, buf_, sizeof buf_); } while (n < 0 && errno == EINTR);
        if (n <= 0) { eof_ = true; len_ = pos_ = 0; return false; }
        pos_ = 0;
        len_ = static_cast<std::size_t>(n);
        return true;
    }
    /// Keeps a partial token contiguous by moving it to the front and reading more.
    void compact() {
        if (eof_) return;
        std::size_t rest = len_ - pos_;
        std::memmove(buf_, buf_ + pos_, rest);
        pos_ = 0;
        len_ = rest;
        if (fd_ == 0) out.flush();
        ssize_t n;
        do { n = ::read(fd_, buf_ + len_, sizeof buf_ - len_); } while (n < 0 && errno == EINTR);
        if (n <= 0) eof_ = true; else len_ += static_cast<std::size_t>(n);
    }
    bool skipSpace() {
        for (;;) {
            while (pos_ < len_ && isSpace(buf_[pos_])) ++pos_;
            if (pos_ < len_) return true;
            if (!fill()) return false;
        }
    }
};

inline Input in{0};

/// Swift's `readLine()`.
inline std::optional<std::string> readLine() { return in.readLine(); }

// MARK: Files

/// A file mapped into memory, read-only: no copy, pages loaded on demand.
class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) detail::throwIO("could not open", path, errno);
        struct stat st;
        if (::fstat(fd, &st) != 0) {
            int e = errno;
            ::close(fd);
            detail::throwIO("could not read", path, e);
        }
        size_ = static_cast<std::size_t>(st.st_size);
        if (size_ > 0) {
            void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) {
                int e = errno;
                ::close(fd);
                detail::throwIO("could not map", path, e);
            }
            data_ = static_cast<const char*>(p);
        }
        ::close(fd);
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& o) noexcept : data_(o.data_), size_(o.size_) { o.data_ = nullptr; o.size_ = 0; }
    ~MappedFile() {
        if (data_) ::munmap(const_cast<char*>(data_), size_);
    }

    std::string_view text() const noexcept { return {data_ ? data_ : "", size_}; }
    const char* data() const noexcept { return data_; }
    Int count() const noexcept { return static_cast<Int>(size_); }

    /// Calls `body(line)` for each line (without the newline).
    template <class F>
    void forEachLine(F&& body) const {
        std::string_view t = text();
        while (!t.empty()) {
            std::size_t nl = t.find('\n');
            std::string_view line = t.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            body(line);
            if (nl == std::string_view::npos) break;
            t.remove_prefix(nl + 1);
        }
    }

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
};

/// The whole file as a string.
inline std::string readFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) detail::throwIO("could not open", path, errno);
    MPP_DEFER { ::close(fd); };
    struct stat st;
    std::string s;
    if (::fstat(fd, &st) == 0 && st.st_size > 0) s.reserve(static_cast<std::size_t>(st.st_size));
    char b[64 * 1024];
    for (;;) {
        ssize_t n = ::read(fd, b, sizeof b);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) detail::throwIO("could not read", path, errno);
        if (n == 0) break;
        s.append(b, static_cast<std::size_t>(n));
    }
    return s;
}

/// Writes `data` to `path`. Atomic by default: the file is written beside
/// the target and renamed over it, so readers never see half a file.
inline void writeFile(const std::string& path, std::string_view data, bool atomically = true) {
    std::string target = atomically ? path + ".mpp-tmp" : path;
    int fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) detail::throwIO("could not create", target, errno);
    const char* p = data.data();
    std::size_t n = data.size();
    while (n > 0) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) {
            int e = errno;
            ::close(fd);
            detail::throwIO("could not write", target, e);
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    if (::close(fd) != 0) detail::throwIO("could not write", target, errno);
    if (atomically && ::rename(target.c_str(), path.c_str()) != 0) detail::throwIO("could not replace", path, errno);
}

inline bool fileExists(const std::string& path) noexcept {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

} // namespace mpp
