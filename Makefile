CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CPPFLAGS += -Iinclude
.PHONY: all test clean sanitize demo
all: build/faultline
build:
	mkdir -p build
build/faultline: src/main.cpp include/faultline/simulator.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/tests: tests/test.cpp include/faultline/simulator.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
test: build/tests build/faultline
	./build/tests
	python3 tests/cli.py
sanitize:
	$(MAKE) clean
	$(MAKE) test CXXFLAGS='-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer'
demo: build/faultline
	python3 scripts/demo.py
clean:
	rm -rf build
