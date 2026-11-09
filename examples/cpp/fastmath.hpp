// A C++ library used from M++ (see examples/cpp/interop.mpp).
#pragma once
#include <mpp.hpp>
#include <string>

namespace fastmath {

/// The mean of `count` doubles.
double mean(const double* values, mpp::Int count);

/// A running statistics accumulator (Welford's method).
struct Stats {
    mpp::Int count = 0;
    double mean = 0, m2 = 0;
    void add(double x) {
        count += 1;
        double d = x - mean;
        mean += d / static_cast<double>(count);
        m2 += d * (x - mean);
    }
    double variance() const { return count > 1 ? m2 / static_cast<double>(count - 1) : 0; }
};

/// "Hello, <name>!" built in C++.
std::string greeting(const char* name);

} // namespace fastmath
