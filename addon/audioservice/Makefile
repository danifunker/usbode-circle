#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS	= audioservice.o

include $(STDLIBHOME)/Config.mk

CFLAGS += -I $(NEWLIBDIR)/include -I $(STDDEF_INCPATH) -I $(STDLIBHOME)/include -I $(CIRCLEHOME)/include -I $(USBODEHOME)/addon

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
