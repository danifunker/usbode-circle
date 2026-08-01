# Host-compiled regression tests for the USBODE SCSI/BOT layer.
#
#   make -C integration-tests            build + run
#   make -C integration-tests build      build only
#   make -C integration-tests WITH_CHD=1 build + run including CHD tests
#
# Compiles the real gadget sources (addon/usbcdgadget + cueparser) and the
# real CUE/BIN/ISO reader (addon/discimage/cuebinfile.cpp) for the build
# machine against the stub Circle headers in harness/stubs/, links them with
# the test harness, and runs the resulting binary. The real-image tests drive
# those readers with actual image files. No cross-compiler or circle-stdlib
# checkout is needed.
#
# CHD support (WITH_CHD=1) additionally compiles addon/discimage/chdfile.cpp
# and libchdr with its vendored zlib/zstd/lzma decoders, so .chd images load
# through the real decompressor. It is opt-in only because those decoders add
# a heavier first-build cost.

CXX ?= c++
CC  ?= cc
CXXFLAGS ?= -O1 -g
CXXFLAGS += -std=c++17 -Wall -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable
CFLAGS   ?= -O1 -g -w

ADDON := ../addon

# Objects live in a directory keyed to the build settings. Neither WITH_CHD
# nor a change of CXXFLAGS is a header dependency, so a shared object
# directory would let an incremental build link objects compiled with
# different settings -- an undefined-CCHDFileDevice link error, or worse a
# binary that silently still contains the other mode's tests, or (the trap
# that motivated the flag hash) a "clean" run that is really measuring
# yesterday's objects after someone added -fsanitize. A timestamp stamp file
# does not fix this: make 3.81 compares mtimes at one-second resolution, so a
# stamp touched in the same second the objects compile looks up to date.
# Distinct directories sidestep mtimes entirely, and each build leaves the
# others intact, so switching needs no `make clean`.
OUT := out
BUILD := $(OUT)/$(if $(filter 1,$(WITH_CHD)),chd,nochd)-$(shell printf '%s' "$(CXXFLAGS) $(CFLAGS)" | cksum | cut -d' ' -f1)
IMAGES := $(OUT)/images

INCLUDES := -Iharness/stubs -I$(ADDON) -Iharness
DEFINES  := -DUSBODE_TESTDATA=\"$(IMAGES)\" -DUSBODE_SDCARD=\"../sdcard\"

# ---------------------------------------------------------------------------
# Real firmware sources under test (compiled from the repo, unchanged)
# ---------------------------------------------------------------------------
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
	$(ADDON)/cueparser/cueparser.cpp \
	$(ADDON)/cueparser/cueutil.cpp

# Real CUE/BIN/ISO and MDS/MDF readers. Their only host-side dependency is
# the FatFs seam (harness/fatfs_host.cpp). No reader logic is reimplemented.
# util.cpp comes in too, so tests mount through the firmware's own factory.
DISCIMAGE_SRCS := \
	$(ADDON)/discimage/cuebinfile.cpp \
	$(ADDON)/discimage/util.cpp \
	$(ADDON)/discimage/mdsfile.cpp \
	$(ADDON)/mdsparser/mdsparser.cpp

# The file log daemon. Not a disc-image path, but it reaches the SD card
# through the same FatFs seam, and what it does when that open FAILS is the
# behaviour worth pinning: a bad path used to cost 20 ms of scheduler time per
# log event, which presented as the whole Pi having gone slow.
SERVICE_SRCS := \
	$(ADDON)/filelogdaemon/filelogdaemon.cpp

CHDR_OBJS :=
ifneq ($(WITH_CHD),1)
# util.cpp's CHD branch needs libchdr; the rest of the factory does not.
DEFINES += -DUSBODE_NO_CHD=1
endif
ifeq ($(WITH_CHD),1)
DISCIMAGE_SRCS += $(ADDON)/discimage/chdfile.cpp
DEFINES += -DWITH_CHD=1
LIBCHDR_DIR  := $(ADDON)/libchdr-src
LIBCHDR_SRCS := \
	$(LIBCHDR_DIR)/src/libchdr_chd.c \
	$(LIBCHDR_DIR)/src/libchdr_cdrom.c \
	$(LIBCHDR_DIR)/src/libchdr_flac.c \
	$(LIBCHDR_DIR)/src/libchdr_huffman.c \
	$(LIBCHDR_DIR)/src/libchdr_bitstream.c
ZLIB_DIR  := $(LIBCHDR_DIR)/deps/zlib-1.3.1
ZLIB_SRCS := $(ZLIB_DIR)/adler32.c $(ZLIB_DIR)/crc32.c $(ZLIB_DIR)/inflate.c \
	$(ZLIB_DIR)/inftrees.c $(ZLIB_DIR)/inffast.c $(ZLIB_DIR)/zutil.c
ZSTD_DIR  := $(LIBCHDR_DIR)/deps/zstd-1.5.6/lib
ZSTD_SRCS := $(wildcard $(ZSTD_DIR)/common/*.c) $(wildcard $(ZSTD_DIR)/decompress/*.c)
LZMA_DIR  := $(LIBCHDR_DIR)/deps/lzma-24.05
LZMA_SRCS := $(LZMA_DIR)/src/LzmaDec.c $(LZMA_DIR)/src/LzmaEnc.c $(LZMA_DIR)/src/LzFind.c \
	$(LZMA_DIR)/src/Sort.c $(LZMA_DIR)/src/Alloc.c $(LZMA_DIR)/src/CpuArch.c \
	$(LZMA_DIR)/src/Delta.c
C_SRCS := $(LIBCHDR_SRCS) $(ZLIB_SRCS) $(ZSTD_SRCS) $(LZMA_SRCS)
LIBCHDR_INC := -I$(LIBCHDR_DIR)/include -I$(ZLIB_DIR) -I$(ZSTD_DIR) -I$(ZSTD_DIR)/common \
	-I$(LZMA_DIR)/include
CHDR_OBJS := $(addprefix $(BUILD)/,$(notdir $(C_SRCS:.c=.o)))
endif

HARNESS_SRCS := $(wildcard harness/*.cpp)
TEST_SRCS    := $(wildcard test-suite/*.cpp)

CXX_SRCS := $(GADGET_SRCS) $(DISCIMAGE_SRCS) $(SERVICE_SRCS) $(HARNESS_SRCS) $(TEST_SRCS)
CXX_OBJS := $(addprefix $(BUILD)/,$(notdir $(CXX_SRCS:.cpp=.o)))
OBJS := $(CXX_OBJS) $(CHDR_OBJS)

VPATH := $(sort $(dir $(CXX_SRCS) $(C_SRCS)))

BINARY := $(BUILD)/usbode-host-tests

# Prepared test images (real files the real readers open at run time).
# The .gz sources live in testdata/ and are decompressed here; the .cue and
# .chd are copied as-is so the readers find them next to their data.
TEST_IMAGES := $(IMAGES)/image.iso \
	$(IMAGES)/realaudio.bin $(IMAGES)/realaudio.cue \
	$(IMAGES)/freedos-test.iso \
	$(IMAGES)/shareware.iso \
	$(IMAGES)/audiocd.bin $(IMAGES)/audiocd.cue \
	$(IMAGES)/mixed.bin $(IMAGES)/mixed.cue \
	$(IMAGES)/mixed.chd \
	$(IMAGES)/videocd-xa.bin \
	$(IMAGES)/audiocd-flac.chd \
	$(IMAGES)/splitaudio-t1.bin $(IMAGES)/splitaudio-t2.bin \
	$(IMAGES)/splitaudio-t3.bin $(IMAGES)/splitaudio.cue \
	$(IMAGES)/splitmissing.cue \
	$(IMAGES)/splitempty-t2.bin $(IMAGES)/splitempty.cue \
	$(IMAGES)/splitmixed-t1.bin $(IMAGES)/splitmixed-t2.bin \
	$(IMAGES)/splitmixed-t3.bin $(IMAGES)/splitmixed.cue \
	$(IMAGES)/stalename.bin $(IMAGES)/stalename.cue \
	$(OUT)/outside.bin $(IMAGES)/splittraversal.cue \
	$(IMAGES)/splitbackslash.cue $(IMAGES)/splitabsolute.cue \
	$(IMAGES)/splitdot.cue $(IMAGES)/tracks/nested-t2.bin \
	$(IMAGES)/splitnested.cue

# Red Book audio is 16-bit stereo at 44.1 kHz, which is exactly what
# sdcard/test.pcm.gz holds, so the sound-test sample the firmware already
# ships doubles as the content of a real CD-DA disc. 1030984 bytes is
# 438 whole 2352-byte sectors with 808 left over; the tail is trimmed so the
# last sector is complete.
PCM_SECTORS := 438

all: run

build: $(BINARY) latest

run: $(BINARY) latest $(TEST_IMAGES)
	./$(BINARY)

# The object directory name carries a hash of the build settings, so the
# binary path is not something a human can type. Keep a stable symlink at
# out/usbode-host-tests pointing at the build this invocation selected, for
# running the suite directly (e.g. USBODE_TEST_VERBOSE=1 out/usbode-host-tests).
#
# This has to run on every invocation rather than as part of the link rule:
# when the binary is already up to date the link rule does not fire, and the
# symlink would be left pointing at whatever mode was built last -- so running
# it would silently exercise the wrong binary.
latest: $(BINARY)
	@ln -sf $(patsubst $(OUT)/%,%,$(BINARY)) $(OUT)/usbode-host-tests

$(BINARY): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# -MMD/-MP emit .d dependency files so edits to a header (a stub, a firmware
# header) trigger the right recompiles; without this an incremental build can
# silently keep a stale object when only a header changed.
$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIBCHDR_INC) $(DEFINES) -MMD -MP -c $< -o $@

# Z7_ST builds the LZMA SDK single-threaded, so LzFindMt (the multithreaded
# matcher) is not pulled in; libchdr only needs the decoder anyway.
# ZSTD_DISABLE_ASM keeps zstd on its portable C decode path: on x86-64 zstd
# otherwise enables an assembly Huffman fast-loop (huf_decompress_amd64.S)
# that we do not build, which fails at link time. The C path is equivalent.
$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DZ7_ST -DZSTD_DISABLE_ASM=1 $(LIBCHDR_INC) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

# Real ISO image: decompress the tracked sdcard/image.iso.gz so the reader
# opens an actual ISO9660 disc.
$(IMAGES)/image.iso: ../sdcard/image.iso.gz
	@mkdir -p $(IMAGES)
	gzip -dc $< > $@

# Real CD-DA disc built from the tracked sdcard/test.pcm.gz, the same sample
# CCDPlayer::SoundTest() plays on hardware. Nothing new is committed: the
# audio is decompressed at build time and cut into three tracks, so the
# reader's per-track offset arithmetic runs over real 16-bit stereo audio
# rather than a generated byte pattern.
$(IMAGES)/realaudio.bin: ../sdcard/test.pcm.gz
	@mkdir -p $(IMAGES)
	gzip -dc $< | head -c $$(($(PCM_SECTORS) * 2352)) > $@

# Tracks at LBA 0, 150 (00:02:00) and 300 (00:04:00); leadout at 438.
$(IMAGES)/realaudio.cue: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "realaudio.bin" BINARY\n' > $@
	@printf '  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' >> $@
	@printf '  TRACK 02 AUDIO\n    INDEX 01 00:02:00\n' >> $@
	@printf '  TRACK 03 AUDIO\n    INDEX 01 00:04:00\n' >> $@

# realaudio.bin cut into one file per track: the redump split-rip layout.
$(IMAGES)/splitaudio-t1.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	dd if=$< of=$@ bs=2352 skip=0 count=150 2>/dev/null

$(IMAGES)/splitaudio-t2.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	dd if=$< of=$@ bs=2352 skip=150 count=150 2>/dev/null

$(IMAGES)/splitaudio-t3.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	dd if=$< of=$@ bs=2352 skip=300 2>/dev/null

$(IMAGES)/splitaudio.cue: $(IMAGES)/splitaudio-t1.bin $(IMAGES)/splitaudio-t2.bin \
                          $(IMAGES)/splitaudio-t3.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "splitaudio-t2.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@
	@printf 'FILE "splitaudio-t3.bin" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# Mixed-mode split rip. Track 1 is real ISO9660 wrapped as MODE1/2352 so LBA
# 16 is a genuine PVD; track 2's file holds 150 pregap sectors before INDEX 01.
$(IMAGES)/splitmixed-t1.bin: $(IMAGES)/image.iso
	@mkdir -p $(IMAGES)
	@python3 -c 'import sys; bcd=lambda v:((v//10)<<4)|(v%10); hdr=lambda i:bytes([bcd((i+150)//4500),bcd(((i+150)//75)%60),bcd((i+150)%75),1]); d=open(sys.argv[1],"rb").read(); o=open(sys.argv[2],"wb"); [o.write(b"\x00"+b"\xff"*10+b"\x00"+hdr(i)+d[i*2048:i*2048+2048].ljust(2048,b"\x00")+b"\x00"*288) for i in range(150)]; o.close()' $< $@

$(IMAGES)/splitmixed-t2.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	dd if=$< of=$@ bs=2352 skip=0 count=300 2>/dev/null

$(IMAGES)/splitmixed-t3.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	dd if=$< of=$@ bs=2352 skip=300 count=138 2>/dev/null

$(IMAGES)/splitmixed.cue: $(IMAGES)/splitmixed-t1.bin $(IMAGES)/splitmixed-t2.bin \
                          $(IMAGES)/splitmixed-t3.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitmixed-t1.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "splitmixed-t2.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n' >> $@
	@printf 'FILE "splitmixed-t3.bin" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# One track file absent: the loader must refuse the mount. The rm keeps the
# second .bin missing by rule rather than by accident.
$(IMAGES)/splitmissing.cue: $(IMAGES)/splitaudio-t1.bin
	@mkdir -p $(IMAGES)
	rm -f $(IMAGES)/splitaudio-gone.bin
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "splitaudio-gone.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# A split rip whose second track file is 0 bytes.
$(IMAGES)/splitempty-t2.bin:
	@mkdir -p $(IMAGES)
	: > $@

$(IMAGES)/splitempty.cue: $(IMAGES)/splitaudio-t1.bin $(IMAGES)/splitempty-t2.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "splitempty-t2.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# A stale FILE line next to a correctly named .bin: the same-stem fallback.
$(IMAGES)/stalename.bin: $(IMAGES)/realaudio.bin
	@mkdir -p $(IMAGES)
	cp $< $@

$(IMAGES)/stalename.cue: $(IMAGES)/stalename.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "renamed elsewhere.bin" BINARY\n' > $@
	@printf '  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' >> $@
	@printf '  TRACK 02 AUDIO\n    INDEX 01 00:02:00\n' >> $@
	@printf '  TRACK 03 AUDIO\n    INDEX 01 00:04:00\n' >> $@

# A real file one level above the image directory, so the traversal cues below
# are rejected for their path and not merely for a missing target.
$(OUT)/outside.bin: $(IMAGES)/splitaudio-t1.bin
	@mkdir -p $(OUT)
	cp $< $@

# Cue sheets whose second FILE escapes the cue's own directory, or is absolute.
$(IMAGES)/splittraversal.cue: $(IMAGES)/splitaudio-t1.bin $(OUT)/outside.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "../outside.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

$(IMAGES)/splitbackslash.cue: $(IMAGES)/splitaudio-t1.bin $(OUT)/outside.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "..\\outside.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

$(IMAGES)/splitabsolute.cue: $(IMAGES)/splitaudio-t1.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "/absolute.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# "." components and nested relative paths stay inside the directory and load.
$(IMAGES)/splitdot.cue: $(IMAGES)/splitaudio-t1.bin $(IMAGES)/splitaudio-t2.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "./splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "./splitaudio-t2.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

$(IMAGES)/tracks/nested-t2.bin: $(IMAGES)/splitaudio-t2.bin
	@mkdir -p $(IMAGES)/tracks
	cp $< $@

$(IMAGES)/splitnested.cue: $(IMAGES)/splitaudio-t1.bin $(IMAGES)/tracks/nested-t2.bin
	@mkdir -p $(IMAGES)
	@printf 'FILE "splitaudio-t1.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n' > $@
	@printf 'FILE "tracks/nested-t2.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n' >> $@

# Real disc-image fixtures under testdata/ (see testdata/README-testdata.md).
# .gz images are decompressed; .cue/.chd are copied as-is next to their data.
$(IMAGES)/%.iso: testdata/%.iso.gz
	@mkdir -p $(IMAGES)
	gzip -dc $< > $@

$(IMAGES)/%.bin: testdata/%.bin.gz
	@mkdir -p $(IMAGES)
	gzip -dc $< > $@

$(IMAGES)/%.cue: testdata/%.cue
	@mkdir -p $(IMAGES)
	cp $< $@

$(IMAGES)/%.chd: testdata/%.chd
	@mkdir -p $(IMAGES)
	cp $< $@

clean:
	rm -rf $(OUT)

.PHONY: all build run clean latest
