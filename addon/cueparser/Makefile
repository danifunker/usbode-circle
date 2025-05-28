#
# Makefile
#

CIRCLEHOME = ../..

OBJS    = cueparser.o

libcueparser.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
