// MppxxCpp: C++ compiled by mpp, with M++'s I/O and safety.
//   mpp examples/hello.cpp
//   mpp --std=c++23 examples/hello.cpp
#include <mpp.hpp>
using namespace mpp;

struct Point {
    Double x, y;
    String description() const { return "Point(x: " + describe(x) + ", y: " + describe(y) + ")"; }
};

int main() {
    print("Hello from", iso::name(), "— hardened:", iso::hardened);

    Array<Int> squares;
    for (Int i : range(1, 6)) squares.append(i * i);
    print("squares:", squares, "sum:", squares.reduce(Int{0}, [](Int a, Int b) { return a + b; }));

    print(Point{1.5, 2}, std::optional<Point>{}, squares.first());

    // One bounds check for the whole loop (like M++'s proven-safe loops).
    Int total = 0;
    squares.withProvenRange(0, squares.count(), [&](Int, Int& v) { total += v; });

    // Everything below is freed at once when `arena` goes away.
    Arena arena;
    auto* names = arena.make<Array<String>>(Array<String>{"Omar", "M++"});
    print("arena:", *names, "bytes:", arena.bytesUsed() > 0);

    print("parallel sum:", parallelReduce<Int>(10'000'000, 0, [](Int i) { return i; },
                                                  [](Int a, Int b) { return a + b; }));
}
