#
# Makefile
#
USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS    = cuebinfile.o mdsfile.o chdfile.o ccdfile.o util.o

$(info *** discimage Makefile Diagnostics ***)
$(info OBJS = $(OBJS))
$(info PWD = $(shell pwd))

libdiscimage.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(USBODEHOME)/addon/libchdr-src/include

-include $(DEPS)