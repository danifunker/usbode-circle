#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS    = cdplayer.o

libcdplayer.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
