// MppxxCpp runtime, in C++23: linked into every program `mpp` builds from
// C++ (and every M++ program that calls C++).
//
// The safety checks `mpp` compiles into C++ (bounds, overflow, division by
// zero) stop the program with a trap instruction: no runtime library, almost
// no code. This turns that trap — and crashes like null dereferences — into a
// Swift-style message, after flushing the program's buffered output.
//
// It installs itself before `main` (a constructor), and finds the C++ side's
// output buffer through two C functions that <mpp.hpp> exports
// (mpp_cxx_flush_output, mpp_cxx_claim_fatal), looked up at start-up so a
// program that doesn't include <mpp.hpp> still links. Everything that runs in
// the signal handler uses static strings and fixed buffers: no allocation.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#include <cxxabi.h>
#include <initializer_list>

#if __cplusplus < 202302L
#error "the MppxxCpp runtime is C++23"
#endif

namespace {

using FlushFn = void (*)();
using ClaimFn = int (*)();

constinit FlushFn flushOutput = nullptr;
constinit ClaimFn claimFatal = nullptr;
constinit const char* programImage = nullptr;
constinit char altStack[64 * 1024]{};

void say(const char* s) noexcept { (void)!::write(2, s, std::strlen(s)); }

template <class F>
F lookup(const char* name) noexcept { return reinterpret_cast<F>(::dlsym(RTLD_DEFAULT, name)); }

/// The faulting address of a SIGSEGV/SIGBUS.
std::uintptr_t faultAddress(const siginfo_t* info) noexcept {
    return info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : UINTPTR_MAX;
}

/// Names of the program's own functions on the stack (the program is
/// linked so that functions with external linkage are named; in optimized
/// code an inlined function shows up as the one it was inlined into).
void printStack() noexcept {
    void* frames[32];
    const int count = ::backtrace(frames, 32);
    bool header = false;
    for (int i = 2; i < count; ++i) {
        Dl_info d{};
        if (::dladdr(frames[i], &d) == 0 || d.dli_sname == nullptr) continue;
        if (programImage && d.dli_fname && std::strcmp(programImage, d.dli_fname) != 0) continue;
        const char* name = d.dli_sname;
        if (std::strcmp(name, "_start") == 0 || std::strncmp(name, "__libc", 6) == 0 || std::strncmp(name, "$s", 2) == 0) continue;
        if (!header) { say("near:\n"); header = true; }
        say("  ");
        int status = -1;
        char* nice = abi::__cxa_demangle(name, nullptr, nullptr, &status);
        say(status == 0 && nice ? nice : name);
        say("\n");
        std::free(nice);
    }
}

void onCrash(int sig, siginfo_t* info, void*) {
    if (flushOutput) flushOutput();
    std::fflush(nullptr);              // C stdio, which M++'s print and printf write through
    const bool first = claimFatal ? claimFatal() != 0 : true;
    if (first) {
        switch (sig) {
        case SIGILL:
        case SIGTRAP:
            say("Fatal error: a safety check failed (index out of range, arithmetic overflow, division by zero, "
                "or a failed standard-library precondition)\n");
            break;
        case SIGFPE: say("Fatal error: division by zero\n"); break;
        case SIGSEGV:
        case SIGBUS:
            say(faultAddress(info) < 4096 ? "Fatal error: null pointer dereferenced\n"
                                          : "Fatal error: invalid memory access (dangling pointer, or stack overflow)\n");
            break;
        default: break;                // SIGABRT: the failed assertion already said what happened
        }
        printStack();
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

[[gnu::constructor]] void install() {
    flushOutput = lookup<FlushFn>("mpp_cxx_flush_output");
    claimFatal = lookup<ClaimFn>("mpp_cxx_claim_fatal");
    if (Dl_info me{}; ::dladdr(reinterpret_cast<void*>(&install), &me) != 0) programImage = me.dli_fname;

    // A separate stack, so stack overflows can still be reported.
    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof altStack;
    ::sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = onCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int s : {SIGILL, SIGTRAP, SIGFPE, SIGSEGV, SIGBUS, SIGABRT}) ::sigaction(s, &action, nullptr);
}

}  // namespace

/// Referenced by the linker flags so the archive member is always kept.
extern "C" [[gnu::used]] const char mpp_runtime_version[] = "MppxxCpp runtime 2 (C++23)";
