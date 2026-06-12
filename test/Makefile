# Host-side unit tests for the pure (non-Circle) parser and subchannel code.
# Run with the host toolchain:  make && make run

CC = cc
CFLAGS = -Wall -g

TESTS = cueparser_test subchannel_test ccdparser_test

all: $(TESTS)

cueparser_test: cueparser_test.cpp ../addon/cueparser/cueparser.cpp ../addon/cueparser/cueparser.h
	$(CC) $(CFLAGS) -o $@ cueparser_test.cpp ../addon/cueparser/cueparser.cpp -lstdc++

subchannel_test: subchannel_test.cpp ../addon/discimage/subchannel.cpp ../addon/discimage/subchannel.h
	$(CC) $(CFLAGS) -o $@ subchannel_test.cpp ../addon/discimage/subchannel.cpp -lstdc++

ccdparser_test: ccdparser_test.cpp ../addon/ccdparser/ccdparser.cpp ../addon/ccdparser/ccdparser.h
	$(CC) $(CFLAGS) -o $@ ccdparser_test.cpp ../addon/ccdparser/ccdparser.cpp -lstdc++

run: $(TESTS)
	./cueparser_test
	./subchannel_test
	./ccdparser_test

clean:
	rm -f $(TESTS)

.PHONY: all run clean
