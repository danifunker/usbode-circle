#
# Makefile
#

CIRCLEHOME = ../..circle

OBJS	= main.o kernel.o webserver.o ftpdaemon.o ftpworker.o util.o

LIBS	= $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/usb/gadget/libusbgadget.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
          $(CIRCLEHOME)/addon/wlan/libwlan.a \
          $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/addon/linux/liblinuxemu.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/addon/discimage/libdiscimage.a \
	  $(CIRCLEHOME)/addon/filelogdaemon/libfilelogdaemon.a \
	  $(CIRCLEHOME)/addon/Properties/libproperties.a \
	  $(CIRCLEHOME)/addon/cueparser/libcueparser.a

include $(CIRCLEHOME)/Rules.mk

%.h: %.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

webserver.o: $(CONTENT)

-include $(DEPS)
