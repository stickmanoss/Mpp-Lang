# Builds M++. The driver is written in M++; a tiny bootstrap (also M++,
# in the subset swiftc accepts directly) compiles it the first time with
# the installed Swift 6 toolchain.
#
#   make              build bin/mpp
#   make test         front-end self-test, then the M++ test suite (debug and release)
#   make bench        compare M++ (and MppxxCpp) I/O with C++ iostreams and stdio
#   make install      link mpp into $(PREFIX)/bin (default /usr/local)
#   make clean

CXX    ?= c++
PREFIX ?= /usr/local
SWIFTC ?= $(or $(MPP_SWIFTC),$(shell command -v swiftc 2>/dev/null),$(shell xcrun --find swiftc 2>/dev/null),swiftc)

DRIVER_SOURCES := $(wildcard MppLang/driver/*.mpp) $(wildcard MppLang/MppPM/PackageManager/*.mpp) \
                  $(wildcard MppLang/mpp-core/Sources/*.mpp) $(wildcard MppLang/mpp-kit/Sources/*.mpp) \
                  $(wildcard MppLang/mpp-cpp/wrapper/*.cpp) $(wildcard MppLang/mpp-cpp/wrapper/*.hpp) MppLang/mpp-cpp/wrapper/mppxx.h

bin/mpp: build/mpp-bootstrap $(DRIVER_SOURCES)
	build/mpp-bootstrap $(CURDIR)

build/mpp-bootstrap: MppLang/bootstrap/bootstrap.mpp
	@mkdir -p build
	@cp $< build/bootstrap.swift
	$(SWIFTC) -O build/bootstrap.swift -o $@

test: bin/mpp
	bin/mpp --self-test
	@for t in tests/*.mpp tests/*.cpp; do \
	  bin/mpp --debug $$t || exit 1; \
	  bin/mpp $$t || exit 1; \
	done
	@bin/mpp --std=c++17 --iso-strict tests/mppxxcpp.cpp && bin/mpp --std=c++23 --iso-strict tests/mppxxcpp.cpp
	@if bin/mpp --version | grep -q "MppxxKotlin): not found"; then echo "(Kotlin not installed: skipping tests/*.kt)"; \
	 else for t in tests/*.kt; do bin/mpp $$t || exit 1; done; fi
	@echo "all tests passed"

bench: bin/mpp
	@mkdir -p build/bench
	$(CXX) -std=c++17 -O2 benchmarks/io_write.cpp -o build/bench/io_write_cpp
	$(CXX) -std=c++17 -O2 benchmarks/io_read.cpp -o build/bench/io_read_cpp
	bin/mpp build benchmarks/io_write.mpp build/bench
	bin/mpp build benchmarks/io_read.mpp build/bench
	build/bench/io_write_cpp > build/bench/nums.txt
	@echo "--- write: C++";  time build/bench/io_write_cpp > /dev/null
	@echo "--- write: M++";  time build/bench/io_write > /dev/null
	@echo "--- read: C++";   time build/bench/io_read_cpp < build/bench/nums.txt
	@echo "--- read: M++";   time build/bench/io_read < build/bench/nums.txt
	bin/mpp benchmarks/tokens.mpp
	bin/mpp benchmarks/cpp_io.cpp
	@bin/mpp --version | grep -q "MppxxKotlin): not found" || bin/mpp benchmarks/kotlin_io.kt

install: bin/mpp
	install -d $(PREFIX)/bin
	ln -sf $(abspath bin/mpp) $(PREFIX)/bin/mpp

clean:
	rm -rf build bin

.PHONY: test bench install clean
