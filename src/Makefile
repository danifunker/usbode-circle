#
# Makefile
#

USBODEHOME = ..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
NEWLIBDIR = $(STDLIBHOME)/install/$(NEWLIB_ARCH)
CIRCLEHOME = $(STDLIBHOME)/libs/circle

OBJS	= main.o kernel.o

include $(STDLIBHOME)/Config.mk
include $(CIRCLEHOME)/Rules.mk

CFLAGS += -I $(NEWLIBDIR)/include -I $(STDDEF_INCPATH) -I $(STDLIBHOME)/include -I $(CIRCLEHOME)/include -I $(USBODEHOME)/addon

LIBS	=  $(NEWLIBDIR)/lib/libm.a \
	$(NEWLIBDIR)/lib/libc.a \
	$(NEWLIBDIR)/lib/libcirclenewlib.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
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
	$(USBODEHOME)/addon/webserver/libwebserver.a \
	$(USBODEHOME)/addon/usbcdgadget/libusbcdgadget.a \
	$(USBODEHOME)/addon/usbmsdgadget/libusbmsdgadget.a \
	$(USBODEHOME)/addon/discimage/libdiscimage.a \
	$(USBODEHOME)/addon/filelogdaemon/libfilelogdaemon.a \
	$(USBODEHOME)/addon/cueparser/libcueparser.a \
	$(USBODEHOME)/addon/ftpserver/libftpserver.a \
	$(USBODEHOME)/addon/cdplayer/libcdplayer.a \
	$(USBODEHOME)/addon/gpiobuttonmanager/libgpiobuttonmanager.a \
	$(USBODEHOME)/addon/display/libdisplay.a

%.h: %.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

-include $(DEPS)
