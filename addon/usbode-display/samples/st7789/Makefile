#
# Makefile
#

CIRCLEHOME = ../../../../circle
USBODEHOME = ../../../..

CFLAGS += -I ../../../../circle/include -I ../../../

OBJS	= main.o kernel.o

LIBS	= $(USBODEHOME)/addon/usbode-display/lib-usbodedisp.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
