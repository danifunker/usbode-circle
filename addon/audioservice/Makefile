#
# Makefile
#

CIRCLEHOME = ../../circle
EXTRALIBS = ../../circle-stdlib/libcircle-stdlib.a

OBJS	= audioservice.o

libaudioservice.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I$(CIRCLEHOME)/addon/fatfs -I../../ -I../../circle-stdlib/include
