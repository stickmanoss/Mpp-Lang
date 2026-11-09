// MppxxCpp — parallel loops: the explicit form of M++'s automatic
// parallelization. Work is split into one contiguous chunk per core.
#pragma once

#include "core.hpp"

#include <thread>
#include <vector>

namespace mpp {

/// The number of hardware threads (at least 1).
inline Int coreCount() noexcept {
    unsigned n = std::thread::hardware_concurrency();
    return n ? static_cast<Int>(n) : 1;
}

/// Calls `body(i)` for every `i` in `0..<count`, spread over all cores.
/// Iterations must be independent. Loops smaller than `grain` run serially.
template <class F>
void parallelFor(Int count, F&& body, Int grain = 4096) {
    if (count < 0) fatalError("parallelFor needs count >= 0");
    Int threads = coreCount();
    if (count < grain * 2 || threads == 1) {
        for (Int i = 0; i < count; ++i) body(i);
        return;
    }
    Int chunks = std::min<Int>(threads, (count + grain - 1) / grain);
    Int per = (count + chunks - 1) / chunks;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(chunks - 1));
    for (Int c = 1; c < chunks; ++c) {
        Int lo = c * per, hi = std::min(count, lo + per);
        pool.emplace_back([&body, lo, hi] { for (Int i = lo; i < hi; ++i) body(i); });
    }
    for (Int i = 0, hi = std::min(count, per); i < hi; ++i) body(i);
    for (auto& t : pool) t.join();
}

/// A parallel map-reduce: `combine(... map(i) ...)` over `0..<count`.
/// `combine` must be associative; `initial` is its identity.
template <class R, class Map, class Combine>
R parallelReduce(Int count, R initial, Map&& map, Combine&& combine, Int grain = 4096) {
    if (count < 0) fatalError("parallelReduce needs count >= 0");
    Int threads = coreCount();
    if (count < grain * 2 || threads == 1) {
        R acc = initial;
        for (Int i = 0; i < count; ++i) acc = combine(acc, map(i));
        return acc;
    }
    Int chunks = std::min<Int>(threads, (count + grain - 1) / grain);
    Int per = (count + chunks - 1) / chunks;
    std::vector<R> partial(static_cast<std::size_t>(chunks), initial);
    std::vector<std::thread> pool;
    auto run = [&](Int c) {
        Int lo = c * per, hi = std::min(count, lo + per);
        R acc = initial;
        for (Int i = lo; i < hi; ++i) acc = combine(acc, map(i));
        partial[static_cast<std::size_t>(c)] = acc;
    };
    for (Int c = 1; c < chunks; ++c) pool.emplace_back(run, c);
    run(0);
    for (auto& t : pool) t.join();
    R acc = initial;
    for (const R& p : partial) acc = combine(acc, p);
    return acc;
}

} // namespace mpp
