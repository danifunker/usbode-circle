#
# Makefile
#

CIRCLEHOME = ../../circle

OBJS    = webserver.o urldecoder.o

CFLAGS += -I ../../addon

libwebserver.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)

%.h: %.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

webserver.o: $(CONTENT)
