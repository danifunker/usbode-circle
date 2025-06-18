#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS	= displaymanager.o st7789display.o chardevice.o st7789device.o sh1106device.o sh1106display.o

libdisplay.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

INCLUDE += -I $(USBODEHOME)/include
CFLAGS += -I $(USBODEHOME)/include

-include $(DEPS)
