#include "checks.hpp"
namespace checks {
mpp::Int sumTo(mpp::Int n) {
    mpp::Int s = 0;
    for (mpp::Int i : mpp::through(1, n)) s += i;
    return s;
}
std::string joined(const char* a, const char* b) { return std::string(a) + "+" + b; }
}
