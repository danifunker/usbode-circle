#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS    = cuebinfile.o

libdiscimage.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
