#
# Makefile for discart module
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS = discart.o tjpgd.o

libdiscart.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(USBODEHOME)/addon

-include $(DEPS)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(DEPS) libdiscart.a
	@rm -f *.d
