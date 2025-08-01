#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS	= sh1106display.o sh1106device.o sh1106device.o sh1106display.o

libsh1106.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

INCLUDE += -I $(USBODEHOME)/include -I $(USBODEHOME)/addon
CFLAGS += -I $(USBODEHOME)/include

-include $(DEPS)
