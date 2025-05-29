#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS    = ftpdaemon.o ftpworker.o

libftpserver.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
