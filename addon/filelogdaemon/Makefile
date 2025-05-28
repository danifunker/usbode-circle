#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS    = filelogdaemon.o

libfilelogdaemon.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
