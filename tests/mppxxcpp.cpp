// MppxxCpp library tests: `mpp tests/mppxxcpp.cpp` (part of `make test`).
#include <mpp.hpp>
#include <cstdio>
using namespace mpp;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct P {
    Int x;
    String description() const { return "P(" + describe(x) + ")"; }
};

int main() {
    // Formatting matches M++/Swift print.
    CHECK(describe(42) == "42");
    CHECK(describe(-7) == "-7");
    CHECK(describe(3.0) == "3.0");
    CHECK(describe(0.1) == "0.1");
    CHECK(describe(1e20) == "1e+20");
    CHECK(describe(true) == "true");
    CHECK(describe(std::optional<Int>(5)) == "Optional(5)");
    CHECK(describe(std::optional<Int>()) == "nil");
    CHECK(describe(Array<Int>{1, 2, 3}) == "[1, 2, 3]");
    CHECK(describe(Array<String>{"a", "b"}) == "[\"a\", \"b\"]");
    CHECK(describe(P{9}) == "P(9)");

    // Array
    Array<Int> a{3, 1, 2};
    a.append(5);
    CHECK(a.count() == 4);
    a.sort();
    CHECK(a == (Array<Int>{1, 2, 3, 5}));
    CHECK(a.removeLast() == 5);
    CHECK(a.first() == std::optional<Int>(1));
    CHECK(a.map([](Int v) { return v * 10; }) == (Array<Int>{10, 20, 30}));
    CHECK(a.filter([](Int v) { return v % 2 == 1; }) == (Array<Int>{1, 3}));
    CHECK(a.reduce(Int{0}, [](Int s, Int v) { return s + v; }) == 6);
    CHECK(a.firstIndex(3) == std::optional<Int>(2));
    Array<Int> b = a;          // value semantics
    b[0] = 99;
    CHECK(a[0] == 1);
    Int sum = 0;
    a.withProvenRange(1, 3, [&](Int, Int& v) { sum += v; });
    CHECK(sum == 5);

    // Span
    Int raw[5] = {1, 2, 3, 4, 5};
    Span<Int> s(raw);
    CHECK(s.count() == 5);
    CHECK(s.slice(1, 3).count() == 2 && s.slice(1, 3)[0] == 2);

    // Ranges and checked integers
    Int n = 0;
    for (Int i : range(0, 10)) n += i;
    CHECK(n == 45);
    for (Int i : through(1, 3)) n += i;
    CHECK(n == 51);
    CHECK(narrow<Int32>(Int{100}) == 100);
    CHECK(wrappingAdd<Int8>(127, 1) == -128);
    CHECK(add<Int>(2, 3) == 5);

    // Arena: destructors run on reset
    static int destroyed = 0;
    struct D { ~D() { ++destroyed; } };
    {
        Arena arena(512);
        for (int i = 0; i < 100; ++i) arena.make<D>();
        auto xs = arena.makeArray<double>(1000);
        CHECK(xs.count() == 1000 && xs[999] == 0.0);
        arena.reset();
        CHECK(destroyed == 100);
        arena.make<D>();
    }
    CHECK(destroyed == 101);

    // defer
    int order = 0;
    {
        MPP_DEFER { order = 2; };
        order = 1;
    }
    CHECK(order == 2);

    // Files: atomic write, read, map, lines
    String path = "/tmp/mppxxcpp-test-" + describe(static_cast<Int>(::getpid())) + ".txt";
    writeFile(path, "one\ntwo\r\nthree");
    CHECK(readFile(path) == "one\ntwo\r\nthree");
    {
        MappedFile f(path);
        Array<String> lines;
        f.forEachLine([&](std::string_view l) { lines.append(String(l)); });
        CHECK(lines == (Array<String>{"one", "two", "three"}));
    }
    ::unlink(path.c_str());
    bool threw = false;
    try { readFile("/nonexistent/mpp"); } catch (const IOError& e) { threw = e.code() == ENOENT; }
    CHECK(threw);

    // Parallel
    CHECK(parallelReduce<Int>(1'000'000, 0, [](Int i) { return i; }, [](Int x, Int y) { return x + y; }) == 499'999'500'000);
    Array<Int> sq(100'000, 0);
    parallelFor(sq.count(), [&](Int i) { sq.data()[i] = i * i; }, 1000);
    CHECK(sq[99'999] == Int{99'999} * 99'999);

    // ISO control
    CHECK(iso::standard() >= 17);
    CHECK(iso::hardened);
    CHECK(iso::pure);

    if (failures) { printTo(err, "MppxxCpp:", failures, "failure(s)"); return 1; }
    print("MppxxCpp: all tests passed (" + String(iso::name()) + ")");
}
