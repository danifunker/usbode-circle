#
# Makefile
#

CIRCLEHOME = ../circle
USBODEHOME = ..

CFLAGS += -I ../include -I ../addon

OBJS	= main.o kernel.o webserver.o util.o displaymanager.o gpiobuttonmanager.o

include $(CIRCLEHOME)/Rules.mk

gitinfo.d:
	@echo "Generating $@..."; \
	./gitinfo.sh

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
	  $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/addon/Properties/libproperties.a \
	  $(USBODEHOME)/lib/usb/gadget/libusbgadget.a \
	  $(USBODEHOME)/addon/discimage/libdiscimage.a \
	  $(USBODEHOME)/addon/filelogdaemon/libfilelogdaemon.a \
	  $(USBODEHOME)/addon/cueparser/libcueparser.a \
	  $(USBODEHOME)/addon/ftpserver/libftpserver.a \
	  $(USBODEHOME)/addon/cdplayer/libcdplayer.a \
	  $(USBODEHOME)/addon/usbode-display/lib-usbodedisp.a

%.h: %.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

webserver.o: $(CONTENT)

-include $(DEPS)
