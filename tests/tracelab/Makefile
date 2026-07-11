# Host-compiled unit tests for addon/tracelab. Builds the real
# traceringbuffer.cpp against lightweight Circle stubs (stubs/) instead of
# the cross toolchain, so these run with a plain host g++/clang++.
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I stubs -I ../../addon

SRCS = test_traceringbuffer.cpp timer_stub.cpp ../../addon/tracelab/traceringbuffer.cpp
BIN = tracelab_tests

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(BIN)
