#
# Makefile for CHD parser
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS = chdparser.o

include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I "../.." -I "$(CIRCLEHOME)" -I "$(STDLIBHOME)/include" -I "$(NEWLIBDIR)/include"
CPPFLAGS += -I "../.." -I "$(CIRCLEHOME)" -I "$(STDLIBHOME)/include" -I "$(NEWLIBDIR)/include" 

libchdparser.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)