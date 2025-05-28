#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS	= st7789display.o chardevice.o st7789device.o sh1106device.o sh1106display.o

DEPS	= $(OBJS:.o=.d)

lib-usbodedisp.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
