#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS    = webserver.o \
	urldecoder.o \
	pagehandlerregistry.o \
	handlers/notfound.o \
	handlers/mountpage.o \
	handlers/homepage.o


libwebserver.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I ../../addon -I ./pages
CFLAGS += -D_TIME_T_DECLARED

handlers/mountpage.o: pages/mount.h
handlers/homepage.o: pages/index.h

pages/%.h: pages/%.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

.PHONY: clean

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(DEPS) libwebserver.a
	@rm -rf handlers/*.d 
	@rm -f *.d 
