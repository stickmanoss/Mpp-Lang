#pragma once
#include <mpp.hpp>
#include <string>
namespace checks {
mpp::Int sumTo(mpp::Int n);
std::string joined(const char* a, const char* b);
struct Counter {
    mpp::Int value = 0;
    void bump(mpp::Int by) { value += by; }
};
}
