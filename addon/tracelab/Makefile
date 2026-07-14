#
# Makefile for tracelab module
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS    = traceringbuffer.o tracelab.o

libtracelab.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(USBODEHOME)/addon

-include $(DEPS)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(DEPS) libtracelab.a
	@rm -f *.d
