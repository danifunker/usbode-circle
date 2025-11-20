#
# Makefile for libchdr (depends on lzma, zlib, zstd)
#
USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

LIBCHDR_SRC = $(USBODEHOME)/addon/libchdr-src/src
LIBCHDR_INC = $(USBODEHOME)/addon/libchdr-src/include

# libchdr core objects only
OBJS    = libchdr_bitstream.o \
          libchdr_cdrom.o \
          libchdr_chd.o \
          libchdr_flac.o \
          libchdr_huffman.o

vpath %.c $(LIBCHDR_SRC)

libchdr.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

# Diagnostics
$(info *** libchdr Makefile Diagnostics ***)
$(info ARCH_MODE = $(ARCH_MODE))
$(info PREFIX = $(PREFIX))
$(info AARCH = $(AARCH))
$(info RASPPI = $(RASPPI))
$(info CC = $(CC))

# Include paths for libchdr and its dependencies
CFLAGS += -I $(LIBCHDR_INC)
CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/deps/lzma-24.05/include
CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/deps/zlib-1.3.1
CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/deps/zstd-1.5.6/lib
CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/deps/zstd-1.5.6/lib/common

# Add defines
CFLAGS += -DPACKAGE_VERSION=\"1.0\"

# Disable NEON optimizations in dr_flac for 32-bit ARM
# Check architecture and disable NEON for 32-bit
ifeq ($(strip $(AARCH)),32)
$(info >>> Disabling NEON and SIMD for 32-bit ARM build)
CFLAGS += -DDR_FLAC_NO_NEON
CFLAGS += -DDR_FLAC_NO_SIMD
else
$(info >>> Not 32-bit build (ARCH_MODE=$(ARCH_MODE)))
endif

$(info CFLAGS = $(CFLAGS))
$(info ***)

-include $(DEPS)