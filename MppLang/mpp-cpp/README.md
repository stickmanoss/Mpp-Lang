# MppxxCpp

C++ with M++'s I/O speed, safety checks and low-level control.

- `include/mpp.hpp` is the umbrella header. It includes:
  - `mpp/core.hpp`: Swift-style names, `fatalError`, `precondition`, checked conversions and arithmetic, ranges, `MPP_DEFER`.
  - `mpp/memory.hpp`: `Array`, `Span`, `Arena`, `Box`, `Shared`.
  - `mpp/io.hpp`: `print`, `Output`, `Input`, `MappedFile`, `readFile`, `writeFile`.
  - `mpp/iso.hpp`: the standard a file is built with, plus `MPP_REQUIRE_ISO`.
  - `mpp/parallel.hpp`: `parallelFor`, `parallelReduce`.
- `runtime/runtime.cpp` (C++23) is linked into every program `mpp` builds from C++. It installs itself before `main` and turns a trapped safety check or a crash into a `Fatal error:` message, after flushing output.

- `wrapper/` (C++23) is the part of `mpp` that handles C++: builds of C++ programs, M++↔C++ interop, and Metallic's shader-to-C++ translation. It is linked into the `mpp` driver, which calls it through `wrapper/mppxx.h`.

`mpp` compiles C++ as C++23 by default, and the M++ interop wrapper always uses C++23 or newer. The headers themselves are plain ISO C++17 and need no runtime, so they also work with older standards and with any compiler. They target macOS and Linux (POSIX I/O). Only through `mpp` do you get the always-on safety flags, the ISO controls and the crash messages.

See "MppxxCpp: C++ through `mpp`" in the top-level README.
