#
# Makefile for ZLIB
#
USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

ZLIB_SRC = $(USBODEHOME)/addon/libchdr-src/deps/zlib-1.3.1

OBJS    = adler32.o \
          crc32.o \
          deflate.o \
          infback.o \
          inffast.o \
          inflate.o \
          inftrees.o \
          trees.o \
          zutil.o

vpath %.c $(ZLIB_SRC)

libz.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(ZLIB_SRC)

-include $(DEPS)