# Configuration
MAKEFLAGS += -j8 --quiet # Use all available CPU cores
USBODEHOME = .
STDLIBHOME = $(USBODEHOME)/circle-stdlib
CIRCLEHOME = $(STDLIBHOME)/libs/circle
DEBUG_FLAGS ?=
DEBUG_CONFIGURE_FLAGS = $(if $(DEBUG_FLAGS),$(addprefix -d ,$(DEBUG_FLAGS)))
DIST_DIR = dist
BASE_VERSION = $(shell cat version.txt | head -n 1 | tr -d '\n\r')
BUILD_NUMBER ?= 
ARCH ?= 32
BUILD_VERSION = $(if $(BUILD_NUMBER),$(BASE_VERSION)-$(BUILD_NUMBER),$(BASE_VERSION))
BRANCH = $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT = $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
ZIP_NAME = usbode-$(BUILD_VERSION)-$(BRANCH)-$(COMMIT).zip
# Read configuration from build-usbode.conf
BUILD_CONF = $(HOME)/build-usbode.conf
PREFIX := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^PathPrefix=' "$(BUILD_CONF)" 2>/dev/null | cut -d'=' -f2; \
	fi \
)
PREFIX64 := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^PathPrefix64=' "$(BUILD_CONF)" 2>/dev/null | cut -d'=' -f2; \
	fi \
)
SUPPORTED_RASPPI64 := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^supported_rasppi_64=' "$(BUILD_CONF)" 2>/dev/null | sed 's/supported_rasppi_64=(//' | sed 's/)//'; \
	fi \
)
SUPPORTED_RASPPI := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^supported_rasppi=' "$(BUILD_CONF)" 2>/dev/null | sed 's/supported_rasppi=(//' | sed 's/)//'; \
	fi \
)
# Fallback if PREFIX is empty
ifeq ($(PREFIX),)
PREFIX = arm-none-eabi-
endif
ifeq ($(PREFIX64),)
PREFIX64 = aarch64-none-elf-
endif
# Fallback if SUPPORTED_RASPPI64 is empty
ifeq ($(SUPPORTED_RASPPI64),)
SUPPORTED_RASPPI64 = 5
endif

RASPPI ?= $(if $(SUPPORTED_RASPPI),$(word 1,$(SUPPORTED_RASPPI)),1)
# Fallback if empty
ifeq ($(SUPPORTED_RASPPI),)
SUPPORTED_RASPPI = 1 2 3 4
endif
#If BUILD_NUMBER is set, set it as the env var required for gitinfo.sh
ifneq ($(BUILD_NUMBER),)
export USBODE_BUILD_NUMBER = $(BUILD_NUMBER)
endif

# Define USBODE addon modules (from /addon directory)
USBODE_ADDONS = gitinfo sdcardservice cdromservice scsitbservice usbcdgadget \
				shutdown usbmsdgadget discimage cueparser filelogdaemon \
				webserver ftpserver display gpiobuttonmanager cdplayer

# Only the Circle addons we actually need
CIRCLE_ADDONS = linux Properties

# Module-specific CPPFLAGS
USBCDGADGET_CPPFLAGS = -DUSB_GADGET_VENDOR_ID=0x04da -DUSB_GADGET_DEVICE_ID_CD=0x0d01

.PHONY: all clean-all clean-dist check-config check-vars configure circle-stdlib\
	 circle-deps circle-addons usbode-addons kernel dist-files
.PHONY: $(USBODE_ADDONS) $(CIRCLE_ADDONS) dist-single multi-arch package release\
	 show-build-info rebuild show-config

all: clean-all clean-dist configure circle-deps circle-addons usbode-addons kernel dist-files

check-vars:
	@if [ -n "$(RASSPI)" ]; then \
		echo "ERROR: Did you mean RASPPI=$(RASSPI) instead of RASSPI=$(RASSPI)?"; \
		exit 1; \
	fi

check-config:
	@if [ ! -f "$(BUILD_CONF)" ]; then \
		if [ "$(ARCH)" = "64" ]; then \
			echo "Warning: $(BUILD_CONF) not found, using default PREFIX64=$(PREFIX64)"; \
		else \
			echo "Warning: $(BUILD_CONF) not found, using default PREFIX=$(PREFIX)"; \
		fi; \
	else \
		if [ "$(ARCH)" = "64" ]; then \
			echo "Using PREFIX64=$(PREFIX64) from $(BUILD_CONF)"; \
		else \
			echo "Using PREFIX=$(PREFIX) from $(BUILD_CONF)"; \
		fi; \
	fi

# Configure Circle for target architecture
configure: check-vars check-config
	@echo "Configuring for RASPPI=$(RASPPI)$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"
	@echo "Using PREFIX=$(PREFIX)"
	git submodule update --init --recursive
	cd $(STDLIBHOME)/build/circle-newlib && \
	make distclean
	./configure -r $(RASPPI) --prefix "$(PREFIX)" $(DEBUG_CONFIGURE_FLAGS)

configure64: check-config check-vars
	@echo "Configuring for RASPPI=$(RASPPI) 64-bit$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"
	@echo "Using PREFIX=$(PREFIX64)"
	git submodule update --init --recursive
	cd $(STDLIBHOME) && \
	make distclean
	./configure -r $(RASPPI) --prefix "$(PREFIX64)" $(DEBUG_CONFIGURE_FLAGS)

# Build Circle stdlib
circle-stdlib: $(if $(filter 64,$(ARCH)),configure64,configure)
	@echo "Building Circle stdlib..."
	cd $(STDLIBHOME) && $(MAKE) clean && $(MAKE) all

# Handle Circle dependencies (firmware and boot files)
circle-deps: circle-stdlib
	@echo "Building Circle dependencies..."
	@if [ ! -f "$(CIRCLEHOME)/addon/wlan/firmware/LICENCE.broadcom_bcm43xx" ]; then \
		echo "Building WLAN firmware..."; \
		cd $(CIRCLEHOME)/addon/wlan/firmware && $(MAKE); \
	fi
	@if [ ! -f "$(CIRCLEHOME)/boot/LICENCE.broadcom" ]; then \
		echo "Building boot files..."; \
		cd $(CIRCLEHOME)/boot && $(MAKE); \
	fi

# Build the two Circle addons we need
circle-addons: circle-deps $(CIRCLE_ADDONS)

$(CIRCLE_ADDONS): circle-deps
	@echo "Building Circle addon: $@"
	cd $(CIRCLEHOME)/addon/$@ && $(MAKE) clean && $(MAKE)

# Build all USBODE addon modules
usbode-addons: circle-addons $(USBODE_ADDONS)

# Special rule for usbcdgadget with custom CPPFLAGS
usbcdgadget: circle-addons
	@echo "Building usbcdgadget with custom flags..."
	cd addon/$@ && $(MAKE) clean && $(MAKE) EXTRA_CPPFLAGS="$(USBCDGADGET_CPPFLAGS)"

# General rule for other USBODE addon modules
$(filter-out usbcdgadget,$(USBODE_ADDONS)): circle-addons
	@echo "Building USBODE addon: $@"
	cd addon/$@ && $(MAKE) clean && $(MAKE)

# Build final kernel
kernel: usbode-addons
	@echo "Building final kernel..."
	cd src && $(MAKE) clean && $(MAKE)

dist-single: check-vars kernel clean-dist
	@echo "Creating single-architecture distribution package for RASPPI=$(RASPPI)..."
	
	# Copy kernel files for current RASPPI architecture
	cp src/kernel*.img $(DIST_DIR)/
	
	# Call the existing dist target (without kernel dependency)
	@$(MAKE) dist-files

dist-pi5:
    @echo "Creating distribution package for Raspberry Pi 5..."
    @$(MAKE) RASPPI=5 ARCH=64 DIST_DIR=dist64 check-vars kernel clean-dist
    @$(MAKE) RASPPI=5 ARCH=64 DIST_DIR=dist64 dist-single
    cp src/kernel*.img dist64/
    @$(MAKE) DIST_DIR=dist64 dist-files

dist-files:
	@echo "Creating distribution package..."
	@echo "Platform Specific Builds Complete Successfully, copying general files to $(DIST_DIR)"
	@mkdir -p $(DIST_DIR)
	# Copy configuration files
	cp sdcard/wpa_supplicant.conf $(DIST_DIR)/
	cp sdcard/cmdline.txt $(DIST_DIR)/
	
	# Create and populate images directory
	mkdir -p $(DIST_DIR)/images
	cp sdcard/image.iso.gz $(DIST_DIR)/images/
	gunzip $(DIST_DIR)/images/image.iso.gz
	
	# Create and populate system directory
	mkdir -p $(DIST_DIR)/system
	cp sdcard/test.pcm.gz $(DIST_DIR)/system/
	gunzip $(DIST_DIR)/system/test.pcm.gz
	
	# Create firmware directory
	mkdir -p $(DIST_DIR)/firmware
	
	# Convert CHD files if chdman is available
	@if command -v chdman >/dev/null 2>&1; then \
		echo "Using chdman to convert CHD files"; \
		chdman extractcd -i sdcard/usbode-audio-test.chd -o $(DIST_DIR)/images/usbode-audio-sampler.cue; \
	else \
		echo "chdman not found, skipping CHD conversion"; \
	fi
	
	# Copy firmware files
	cp $(CIRCLEHOME)/addon/wlan/firmware/* $(DIST_DIR)/firmware/
	rm -f $(DIST_DIR)/firmware/Makefile
	
	# Copy boot files
	mkdir -p $(DIST_DIR)/overlays
	cp $(CIRCLEHOME)/boot/bootcode.bin $(DIST_DIR)/
	@for f in start.elf start4.elf; do \
        if [ -f "$(CIRCLEHOME)/boot/$$f" ]; then cp "$(CIRCLEHOME)/boot/$$f" $(DIST_DIR)/; fi; \
    done
	@for f in fixup.dat fixup4.dat; do \
        if [ -f "$(CIRCLEHOME)/boot/$$f" ]; then cp "$(CIRCLEHOME)/boot/$$f" $(DIST_DIR)/; fi; \
    done
	cp $(CIRCLEHOME)/boot/bcm*.dtb $(DIST_DIR)/
	cp $(CIRCLEHOME)/boot/bcm*.dtbo $(DIST_DIR)/overlays
	cp $(CIRCLEHOME)/boot/LICENCE.broadcom $(DIST_DIR)/
	cp $(CIRCLEHOME)/boot/COPYING.linux $(DIST_DIR)/firmware/

	# Create config.txt (hardcoded to 32-bit for now)
	cp $(CIRCLEHOME)/boot/config$(ARCH).txt $(DIST_DIR)/config.txt	# Remove problematic lines from pi4 config.txt
	sed -i.bak -e 's/^\(armstub=armstub7-rpi4\.bin\)/#\1/' -e 's/^\(max_framebuffers=2\)/#\1/' $(DIST_DIR)/config.txt && rm $(DIST_DIR)/config.txt.bak
	cat sdcard/config-usbode.txt >> $(DIST_DIR)/config.txt
	cp sdcard/config-options.txt $(DIST_DIR)/
	cp sdcard/cmdline.txt $(DIST_DIR)/
	
	# Create zip file
	@echo "Creating $(ZIP_NAME)..."
	@cd $(DIST_DIR) && zip -r ../$(ZIP_NAME) ./*
	@echo "Built $(ZIP_NAME). Copy the contents of the zip file to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"

# Clean everything
clean-all:
	@echo "Cleaning all modules..."
	@rm -rf $(DIST_DIR)
	@rm -f usbode*.zip
	@cd src && $(MAKE) clean || true
	@for module in $(USBODE_ADDONS); do \
		echo "Cleaning addon/$$module"; \
		cd addon/$$module && $(MAKE) clean || true; \
		cd ../..; \
	done
	@for addon in $(CIRCLE_ADDONS); do \
		echo "Cleaning $(CIRCLEHOME)/addon/$$addon"; \
		cd $(CIRCLEHOME)/addon/$$addon && $(MAKE) clean || true; \
	done
	@cd $(STDLIBHOME) && $(MAKE) clean || true

# Override Circle's clean to call our clean-all
clean: clean-all

# Clean distribution files and prepare fresh dist directory
clean-dist:
	@echo "Cleaning distribution directory..."
	@rm -rf $(DIST_DIR)
	@rm -f usbode*.zip
	@mkdir -p $(DIST_DIR)

# Multi-architecture build (matches your build script)
multi-arch: clean-dist
	@for arch in $(SUPPORTED_RASPPI); do \
		echo "Building for RASPPI=$$arch$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"; \
		$(MAKE) RASPPI=$$arch DEBUG_FLAGS="$(DEBUG_FLAGS)" configure circle-deps circle-addons usbode-addons kernel; \
		cp src/kernel*.img $(DIST_DIR)/ 2>/dev/null || true; \
	done
	@$(MAKE) dist-files

multi-arch64: clean-dist
	@for arch in $(SUPPORTED_RASPPI64); do \
		echo "Building for RASPPI=$$arch$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"; \
		$(MAKE) RASPPI=$$arch DEBUG_FLAGS="$(DEBUG_FLAGS)" configure64 circle-deps circle-addons usbode-addons kernel; \
		cp src/kernel*.img $(DIST_DIR)/ 2>/dev/null || true; \
	done
	@$(MAKE) dist-files

package: multi-arch

package64: multi-arch64

release: 
	@if [ -z "$(BUILD_NUMBER)" ]; then \
		echo "Error: BUILD_NUMBER not set. Use: make release BUILD_NUMBER=123"; \
		exit 1; \
	fi
	@$(MAKE) multi-arch BUILD_NUMBER="$(BUILD_NUMBER)"

show-build-info:
	@echo "BASE_VERSION = $(BASE_VERSION)"
	@echo "BUILD_NUMBER = $(BUILD_NUMBER)"
	@echo "BUILD_VERSION = $(BUILD_VERSION)"
	@echo "BRANCH = $(BRANCH)"
	@echo "COMMIT = $(COMMIT)"
	@echo "ZIP_NAME = $(ZIP_NAME)"


# Development helpers
rebuild: clean-all all

# Show what we're building
show-config:
	@echo "Current RASPPI = $(RASPPI)"
	@echo "SUPPORTED_RASPPI = $(SUPPORTED_RASPPI)"
	@echo "PREFIX = $(PREFIX)"
	@echo "DEBUG_FLAGS = $(DEBUG_FLAGS)"
	@echo "CIRCLE_ADDONS = $(CIRCLE_ADDONS)"
	@echo "USBODE_ADDONS = $(USBODE_ADDONS)"
	@echo "USBCDGADGET_CPPFLAGS = $(USBCDGADGET_CPPFLAGS)"