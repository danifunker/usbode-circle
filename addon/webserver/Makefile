#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS    = webserver.o \
	util.o \
	pagehandlerregistry.o \
	handlers/pagehandlerbase.o \
	handlers/mountpage.o \
	handlers/modepage.o \
	handlers/homepage.o \
	handlers/asset.o 

libwebserver.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I ../../addon -I ./pages -I ./assets -I .
CFLAGS += -D_TIME_T_DECLARED

handlers/pagehandlerbase.o: pages/template.h
handlers/mountpage.o: pages/mount.h
handlers/modepage.o: pages/mode.h
handlers/homepage.o: pages/index.h
handlers/asset.o: assets/logo.h
handlers/asset.o: assets/favicon.h

pages/%.h: pages/%.html
	@echo "  CONVERTTOOL   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

assets/%.h: assets/%.ico
	@echo "  XXD   $@"
	@xxd -i $< $@

assets/%.h: assets/%.jpg
	@echo "  XXD   $@"
	@xxd -i $< $@

.PHONY: clean

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(DEPS) libwebserver.a
	@rm -rf handlers/*.d 
	@rm -f *.d 
