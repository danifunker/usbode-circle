#
# Makefile
#

CIRCLEHOME = ../../circle

CFLAGS += -I ../../addon
OBJS	= gpiobuttonmanager.o

DEPS	= $(OBJS:.o=.d)

libgpiobuttonmanager.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
