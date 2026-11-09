#include "fastmath.hpp"

namespace fastmath {

double mean(const double* values, mpp::Int count) {
    mpp::precondition(count > 0, "mean of an empty list");
    mpp::Span<const double> s(values, count);
    double total = 0;
    s.forEach([&](double v) { total += v; });
    return total / static_cast<double>(count);
}

std::string greeting(const char* name) { return std::string("Hello, ") + name + "!"; }

} // namespace fastmath
