#
# Makefile for LZMA
#
USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

LZMA_SRC = $(USBODEHOME)/addon/libchdr-src/deps/lzma-24.05/src

OBJS    = Alloc.o \
          CpuArch.o \
          LzmaDec.o

vpath %.c $(LZMA_SRC)

liblzma.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/deps/lzma-24.05/include
CFLAGS += -DZ7_ST # Disable multithreading (single-threaded mode)

-include $(DEPS)