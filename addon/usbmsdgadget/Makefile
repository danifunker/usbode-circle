#
# Makefile
#
# Circle - A C++ bare metal environment for Raspberry Pi
# Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

CIRCLEHOME = ../../circle

all: libusbmsdgadget.a

CPPFLAGS += -DUSB_GADGET_VENDOR_ID=0x04da
CPPFLAGS += -DUSB_GADGET_DEVICE_ID_MMSD=0x1d01
CFLAGS += -I ../../addon

include $(CIRCLEHOME)/Rules.mk

ifneq ($(strip $(RASPPI)),5)
OBJS    = usbmsdgadget.o usbmsdgadgetendpoint.o

endif

libusbmsdgadget.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

-include $(DEPS)
