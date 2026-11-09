// MppxxCpp I/O vs C stdio vs iostreams: `mpp benchmarks/cpp_io.cpp`
// Writes and reads 10 million numbers through each (to /dev/null and a temp file).
#include <mpp.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
using namespace mpp;

template <class F>
double timed(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    const Int N = 10'000'000;
    const String file = "/tmp/mppxxcpp-bench.txt";

    double wMpp = timed([&] {
        int fd = ::open("/dev/null", O_WRONLY);
        {
            Output o(fd);
            for (Int i = 0; i < N; ++i) write(o, i, ' ', 0.5 * static_cast<double>(i), '\n');
        }
        ::close(fd);
    });
    double wStdio = timed([&] {
        std::FILE* f = std::fopen("/dev/null", "w");
        for (Int i = 0; i < N; ++i) std::fprintf(f, "%lld %g\n", static_cast<long long>(i), 0.5 * static_cast<double>(i));
        std::fclose(f);
    });
    double wStream = timed([&] {
        std::ofstream f("/dev/null");
        for (Int i = 0; i < N; ++i) f << i << ' ' << 0.5 * static_cast<double>(i) << '\n';
    });

    {
        int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        Output o(fd);
        for (Int i = 0; i < N; ++i) write(o, i, '\n');
        o.flush();
        ::close(fd);
    }
    Int s1 = 0, s2 = 0, s3 = 0;
    double rMpp = timed([&] {
        int fd = ::open(file.c_str(), O_RDONLY);
        auto in = box<Input>(fd);
        while (auto v = in->readInt()) s1 += *v;
        ::close(fd);
    });
    double rStdio = timed([&] {
        std::FILE* f = std::fopen(file.c_str(), "r");
        long long v;
        while (std::fscanf(f, "%lld", &v) == 1) s2 += v;
        std::fclose(f);
    });
    double rStream = timed([&] {
        std::ifstream f(file);
        long long v;
        while (f >> v) s3 += v;
    });
    ::unlink(file.c_str());
    precondition(s1 == s2 && s2 == s3, "all readers agree");

    std::printf("10M numbers         MppxxCpp   C stdio   iostream\n");
    std::printf("write (int+double)  %6.3fs   %6.3fs   %6.3fs   (%.1fx / %.1fx faster)\n", wMpp, wStdio, wStream, wStdio / wMpp, wStream / wMpp);
    std::printf("read  (int)         %6.3fs   %6.3fs   %6.3fs   (%.1fx / %.1fx faster)\n", rMpp, rStdio, rStream, rStdio / rMpp, rStream / rMpp);
}
