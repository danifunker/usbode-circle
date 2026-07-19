# Host-compiled regression tests for the USBODE SCSI/BOT layer.
#
#   make -C test/host        build + run
#   make -C test/host build  build only
#
# Compiles the real gadget sources (addon/usbcdgadget + cueparser) for the
# build machine against the stub Circle headers in stubs/, links them with
# the test harness, and runs the resulting binary. No cross-compiler or
# circle-stdlib checkout needed.

CXX ?= c++
CXXFLAGS ?= -O1 -g
CXXFLAGS += -std=c++17 -Wall -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable

ADDON := ../../addon
BUILD := out

INCLUDES := -Istubs -I$(ADDON) -Iharness

GADGET_SRCS := \
	$(ADDON)/usbcdgadget/usbcdgadget.cpp \
	$(ADDON)/usbcdgadget/usbcdgadgetendpoint.cpp \
	$(ADDON)/usbcdgadget/tcdstate_update.cpp \
	$(ADDON)/usbcdgadget/scsi_inquiry.cpp \
	$(ADDON)/usbcdgadget/scsi_read.cpp \
	$(ADDON)/usbcdgadget/scsi_toc.cpp \
	$(ADDON)/usbcdgadget/scsi_misc.cpp \
	$(ADDON)/usbcdgadget/scsi_toolbox.cpp \
	$(ADDON)/usbcdgadget/cd_utils.cpp \
	$(ADDON)/cueparser/cueparser.cpp

HARNESS_SRCS := $(wildcard harness/*.cpp)
TEST_SRCS := $(wildcard tests/*.cpp)

SRCS := $(GADGET_SRCS) $(HARNESS_SRCS) $(TEST_SRCS)

# Map every source path to a flat object name in build/.
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.cpp=.o)))

VPATH := $(sort $(dir $(SRCS)))

BINARY := $(BUILD)/usbode-host-tests

all: run

build: $(BINARY)

run: $(BINARY)
	./$(BINARY)

$(BINARY): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all build run clean
