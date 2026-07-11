# Host-compiled unit tests for addon/cueparser/cueutil.cpp.
# Runs with a plain host compiler; no Circle/cross-toolchain needed.
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I ../../addon

SRCS = test_cueutil.cpp ../../addon/cueparser/cueparser.cpp ../../addon/cueparser/cueutil.cpp
BIN = cueutil_tests

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(BIN)
