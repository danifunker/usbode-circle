#
# Makefile
#

USBODEHOME = $(realpath ../..)
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS    = displayservice.o \
	pagemanager.o \
	st7789/splashpage.o \
	st7789/homepage.o \
	st7789/imagespage.o \
	st7789/powerpage.o \
	st7789/configpage.o \
	st7789/usbconfigpage.o \
	st7789/logconfigpage.o \
	st7789/timeoutconfigpage.o \
	st7789/infopage.o \
	st7789/infopage.o \
	st7789/display.o \
	st7789/upgradepage.o \
	st7789/setuppage.o \
	st7789/classicmacmodepage.o \
	st7789/soundconfig.o \
	st7789/lowpowertimeoutconfigpage.o \
	sh1106/display.o \
	sh1106/homepage.o \
	sh1106/powerpage.o \
	sh1106/configpage.o \
	sh1106/usbconfigpage.o \
	sh1106/logconfigpage.o \
	sh1106/timeoutconfigpage.o \
	sh1106/imagespage.o \
	sh1106/infopage.o \
	sh1106/setuppage.o \
	sh1106/upgradepage.o \
	sh1106/classicmacmodepage.o \
	sh1106/soundconfig.o

libdisplayservice.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I ../../addon

#$(info CFLAGS are: $(CFLAGS))

-include $(DEPS)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(DEPS) libdisplayservice.a
	@rm -rf st7789/*.d
	@rm -f *.d
