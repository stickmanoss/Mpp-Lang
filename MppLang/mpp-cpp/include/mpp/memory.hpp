// MppxxCpp — memory: checked Array and Span, proven-safe loops, Arena, Box.
#pragma once

#include "core.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace mpp {

template <class T>
class Span;

namespace detail {
[[noreturn]] inline void indexOutOfRange(Int i, Int count, const char* file, int line) noexcept {
    (void)i;
    (void)count;
    fatalError("Index out of range", file, line);
}
} // namespace detail

/// A view of `count` contiguous elements. Every `[i]` is checked; a loop that
/// goes through `forEach`/`withProvenRange` is checked once, up front.
template <class T>
class Span {
public:
    constexpr Span() noexcept = default;
    constexpr Span(T* start, Int count) noexcept : p_(start), n_(count) {}
    template <std::size_t N>
    constexpr Span(T (&a)[N]) noexcept : p_(a), n_(static_cast<Int>(N)) {}

    constexpr Int count() const noexcept { return n_; }
    constexpr bool isEmpty() const noexcept { return n_ == 0; }
    constexpr T* data() const noexcept { return p_; }
    constexpr T* begin() const noexcept { return p_; }
    constexpr T* end() const noexcept { return p_ + n_; }

    T& operator()(Int i, const char* file = __builtin_FILE(), int line = __builtin_LINE()) const noexcept {
        if (static_cast<UInt>(i) >= static_cast<UInt>(n_)) detail::indexOutOfRange(i, n_, file, line);
        return p_[i];
    }
    T& operator[](Int i) const noexcept {
        if (static_cast<UInt>(i) >= static_cast<UInt>(n_)) detail::indexOutOfRange(i, n_, "", 0);
        return p_[i];
    }

    /// `s[a..<b]`, checked.
    Span slice(Int from, Int to, const char* file = __builtin_FILE(), int line = __builtin_LINE()) const noexcept {
        if (from < 0 || to < from || to > n_) fatalError("Slice out of range", file, line);
        return Span(p_ + from, to - from);
    }

    std::optional<T> first() const { return n_ ? std::optional<T>(p_[0]) : std::nullopt; }
    std::optional<T> last() const { return n_ ? std::optional<T>(p_[n_ - 1]) : std::nullopt; }

    /// Calls `body(i, element)` for `i` in `from..<to`. The range is checked
    /// once against the span, so the loop itself has no bounds checks — the
    /// same proof M++ makes for `for i in a..<b { x[i] }`.
    template <class F>
    void withProvenRange(Int from, Int to, F&& body, const char* file = __builtin_FILE(),
                         int line = __builtin_LINE()) const {
        if (from < 0 || to < from || to > n_) fatalError("Index out of range", file, line);
        T* p = p_;
        for (Int i = from; i < to; ++i) body(i, p[i]);
    }
    template <class F>
    void forEach(F&& body) const {
        for (Int i = 0; i < n_; ++i) body(p_[i]);
    }
    template <class F>
    void forEachIndexed(F&& body) const {
        for (Int i = 0; i < n_; ++i) body(i, p_[i]);
    }

    operator Span<const T>() const noexcept { return Span<const T>(p_, n_); }

private:
    T* p_ = nullptr;
    Int n_ = 0;
};

/// Swift's `Array`: a growable value type (copying copies), every index checked.
template <class T>
class Array {
public:
    Array() = default;
    Array(std::initializer_list<T> items) : v_(items) {}
    Array(Int count, const T& repeating) : v_(static_cast<std::size_t>(checkCount(count)), repeating) {}
    explicit Array(std::vector<T> v) : v_(std::move(v)) {}
    template <class It>
    Array(It first, It last) : v_(first, last) {}

    // Size
    Int count() const noexcept { return static_cast<Int>(v_.size()); }
    bool isEmpty() const noexcept { return v_.empty(); }
    Int capacity() const noexcept { return static_cast<Int>(v_.capacity()); }
    void reserveCapacity(Int n) { v_.reserve(static_cast<std::size_t>(checkCount(n))); }

    // Access
    T& operator[](Int i) noexcept { return at(i, "", 0); }
    const T& operator[](Int i) const noexcept { return at(i, "", 0); }
    /// `a(i)` is `a[i]` with the caller's file and line in the error.
    T& operator()(Int i, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept { return at(i, file, line); }
    const T& operator()(Int i, const char* file = __builtin_FILE(), int line = __builtin_LINE()) const noexcept {
        return at(i, file, line);
    }
    std::optional<T> first() const { return v_.empty() ? std::nullopt : std::optional<T>(v_.front()); }
    std::optional<T> last() const { return v_.empty() ? std::nullopt : std::optional<T>(v_.back()); }

    // Mutation
    void append(const T& x) { v_.push_back(x); }
    void append(T&& x) { v_.push_back(std::move(x)); }
    template <class... A>
    T& emplace(A&&... a) { return v_.emplace_back(std::forward<A>(a)...); }
    void append(const Array& other) { v_.insert(v_.end(), other.v_.begin(), other.v_.end()); }
    void insert(T x, Int at, const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        if (at < 0 || at > count()) detail::indexOutOfRange(at, count(), file, line);
        v_.insert(v_.begin() + at, std::move(x));
    }
    T remove(Int at, const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        if (static_cast<UInt>(at) >= static_cast<UInt>(count())) detail::indexOutOfRange(at, count(), file, line);
        T x = std::move(v_[static_cast<std::size_t>(at)]);
        v_.erase(v_.begin() + at);
        return x;
    }
    T removeLast(const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        if (v_.empty()) fatalError("Can't remove last element from an empty collection", file, line);
        T x = std::move(v_.back());
        v_.pop_back();
        return x;
    }
    std::optional<T> popLast() {
        if (v_.empty()) return std::nullopt;
        T x = std::move(v_.back());
        v_.pop_back();
        return x;
    }
    void removeAll(bool keepingCapacity = false) {
        v_.clear();
        if (!keepingCapacity) v_.shrink_to_fit();
    }
    void swapAt(Int i, Int j, const char* file = __builtin_FILE(), int line = __builtin_LINE()) noexcept {
        std::swap(at(i, file, line), at(j, file, line));
    }
    void sort() { std::sort(v_.begin(), v_.end()); }
    template <class Less>
    void sort(Less&& less) { std::sort(v_.begin(), v_.end(), std::forward<Less>(less)); }
    void reverse() { std::reverse(v_.begin(), v_.end()); }

    // Higher-order
    template <class F>
    auto map(F&& f) const -> Array<std::decay_t<decltype(f(std::declval<const T&>()))>> {
        Array<std::decay_t<decltype(f(std::declval<const T&>()))>> out;
        out.reserveCapacity(count());
        for (const T& x : v_) out.append(f(x));
        return out;
    }
    template <class F>
    Array filter(F&& keep) const {
        Array out;
        for (const T& x : v_) if (keep(x)) out.append(x);
        return out;
    }
    template <class R, class F>
    R reduce(R initial, F&& combine) const {
        for (const T& x : v_) initial = combine(std::move(initial), x);
        return initial;
    }
    bool contains(const T& x) const { return std::find(v_.begin(), v_.end(), x) != v_.end(); }
    std::optional<Int> firstIndex(const T& x) const {
        auto it = std::find(v_.begin(), v_.end(), x);
        return it == v_.end() ? std::nullopt : std::optional<Int>(static_cast<Int>(it - v_.begin()));
    }

    // Views
    Span<T> span() noexcept { return Span<T>(v_.data(), count()); }
    Span<const T> span() const noexcept { return Span<const T>(v_.data(), count()); }
    template <class F>
    void withProvenRange(Int from, Int to, F&& body, const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
        span().withProvenRange(from, to, std::forward<F>(body), file, line);
    }
    T* data() noexcept { return v_.data(); }
    const T* data() const noexcept { return v_.data(); }
    auto begin() noexcept { return v_.begin(); }
    auto end() noexcept { return v_.end(); }
    auto begin() const noexcept { return v_.begin(); }
    auto end() const noexcept { return v_.end(); }
    std::vector<T>& vector() noexcept { return v_; }
    const std::vector<T>& vector() const noexcept { return v_; }

    bool operator==(const Array& o) const { return v_ == o.v_; }
    bool operator!=(const Array& o) const { return v_ != o.v_; }

private:
    std::vector<T> v_;

    T& at(Int i, const char* file, int line) noexcept {
        if (static_cast<UInt>(i) >= static_cast<UInt>(v_.size())) detail::indexOutOfRange(i, count(), file, line);
        return v_[static_cast<std::size_t>(i)];
    }
    const T& at(Int i, const char* file, int line) const noexcept {
        if (static_cast<UInt>(i) >= static_cast<UInt>(v_.size())) detail::indexOutOfRange(i, count(), file, line);
        return v_[static_cast<std::size_t>(i)];
    }
    static Int checkCount(Int n) noexcept {
        if (n < 0) fatalError("Can't construct Array with count < 0");
        return n;
    }
};

// MARK: Ownership

/// A uniquely owned heap value (Swift's `final class` with one owner).
template <class T>
using Box = std::unique_ptr<T>;
template <class T, class... A>
Box<T> box(A&&... a) { return std::make_unique<T>(std::forward<A>(a)...); }

/// A shared, reference-counted value.
template <class T>
using Shared = std::shared_ptr<T>;
template <class T, class... A>
Shared<T> shared(A&&... a) { return std::make_shared<T>(std::forward<A>(a)...); }

// MARK: Arena

/// A bump allocator: allocation is a pointer increment, and everything is
/// freed at once by `reset()` or when the arena goes away. Destructors of
/// non-trivial objects made with `make` run then, newest first.
class Arena {
public:
    explicit Arena(std::size_t blockSize = 64 * 1024) noexcept : blockSize_(blockSize < 256 ? 256 : blockSize) {}
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    ~Arena() { release(); }

    /// Raw, uninitialized bytes.
    void* allocate(std::size_t bytes, std::size_t align = alignof(std::max_align_t)) {
        if (align == 0 || (align & (align - 1)) != 0) fatalError("Arena alignment must be a power of two");
        std::uintptr_t p = (cur_ + (align - 1)) & ~(static_cast<std::uintptr_t>(align) - 1);
        if (cur_ == 0 || p + bytes > end_) {
            newBlock(bytes + align);
            p = (cur_ + (align - 1)) & ~(static_cast<std::uintptr_t>(align) - 1);
        }
        cur_ = p + bytes;
        used_ += bytes;
        return reinterpret_cast<void*>(p);
    }

    /// Constructs a `T` in the arena.
    template <class T, class... A>
    T* make(A&&... a) {
        void* mem = allocate(sizeof(T), alignof(T));
        T* obj = ::new (mem) T(std::forward<A>(a)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            auto* d = static_cast<Dtor*>(allocate(sizeof(Dtor), alignof(Dtor)));
            d->obj = obj;
            d->destroy = [](void* o) { static_cast<T*>(o)->~T(); };
            d->next = dtors_;
            dtors_ = d;
        }
        return obj;
    }

    /// An uninitialized array of `count` trivially-constructible values.
    template <class T>
    Span<T> makeArray(Int count) {
        static_assert(std::is_trivially_destructible_v<T>, "makeArray holds trivially destructible values");
        if (count < 0) fatalError("Arena array count < 0");
        T* p = static_cast<T*>(allocate(sizeof(T) * static_cast<std::size_t>(count), alignof(T)));
        for (Int i = 0; i < count; ++i) ::new (p + i) T();
        return Span<T>(p, count);
    }

    /// Frees everything; keeps the first block for reuse.
    void reset() {
        runDestructors();
        if (!head_) return;
        Block* keep = head_;
        while (keep->next) keep = keep->next;
        for (Block* b = head_; b && b != keep;) {
            Block* n = b->next;
            std::free(b);
            b = n;
        }
        head_ = keep;
        keep->next = nullptr;
        cur_ = reinterpret_cast<std::uintptr_t>(keep + 1);
        end_ = cur_ + keep->size;
        used_ = 0;
    }

    /// Bytes handed out since the last reset.
    std::size_t bytesUsed() const noexcept { return used_; }

private:
    struct Block {
        Block* next;
        std::size_t size;
        std::max_align_t pad[1];
    };
    struct Dtor {
        void* obj;
        void (*destroy)(void*);
        Dtor* next;
    };
    std::size_t blockSize_;
    Block* head_ = nullptr;
    std::uintptr_t cur_ = 0, end_ = 0;
    std::size_t used_ = 0;
    Dtor* dtors_ = nullptr;

    void newBlock(std::size_t atLeast) {
        std::size_t size = atLeast > blockSize_ ? atLeast : blockSize_;
        auto* b = static_cast<Block*>(std::malloc(sizeof(Block) + size));
        if (!b) throw std::bad_alloc();
        b->next = head_;
        b->size = size;
        head_ = b;
        cur_ = reinterpret_cast<std::uintptr_t>(b + 1);
        end_ = cur_ + size;
    }
    void runDestructors() noexcept {
        for (Dtor* d = dtors_; d; d = d->next) d->destroy(d->obj);
        dtors_ = nullptr;
    }
    void release() noexcept {
        runDestructors();
        for (Block* b = head_; b;) {
            Block* n = b->next;
            std::free(b);
            b = n;
        }
        head_ = nullptr;
        cur_ = end_ = 0;
    }
};

} // namespace mpp
