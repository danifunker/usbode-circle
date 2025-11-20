#
# Makefile for ZSTD
#
USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

ZSTD_LIB = $(USBODEHOME)/addon/libchdr-src/deps/zstd-1.5.6/lib

# Common objects
OBJS    = debug.o \
          entropy_common.o \
          error_private.o \
          fse_decompress.o \
          xxhash.o \
          zstd_common.o \
          pool.o \
          threading.o

# Compress objects
OBJS   += hist.o \
          huf_compress.o \
          zstd_compress.o \
          zstd_compress_literals.o \
          zstd_compress_sequences.o \
          zstd_compress_superblock.o \
          zstd_double_fast.o \
          zstd_fast.o \
          zstd_lazy.o \
          zstd_ldm.o \
          zstd_opt.o \
          zstdmt_compress.o

# Decompress objects
OBJS   += huf_decompress.o \
          zstd_ddict.o \
          zstd_decompress.o \
          zstd_decompress_block.o

vpath %.c $(ZSTD_LIB)/common
vpath %.c $(ZSTD_LIB)/compress
vpath %.c $(ZSTD_LIB)/decompress

libzstd.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(ZSTD_LIB)
CFLAGS += -I $(ZSTD_LIB)/common

-include $(DEPS)