#
# Makefile
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS = gitinfo.o

libgitinfo.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

gitinfo.o: gitinfo.h

gitinfo.h: gitinfo.sh
	@echo "  SH    $@"
	@AARCH=$(AARCH) RASPPI=$(RASPPI) ./gitinfo.sh

.PHONY: clean

clean:
	@echo "  CLEAN"
	@rm -f gitinfo.h
	@rm -f $(OBJS) $(DEPS) libgitinfo.a
	@rm -f *.d

