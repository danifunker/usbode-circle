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
SUPPORTED_RASPPI := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^supported_rasppi=' "$(BUILD_CONF)" 2>/dev/null | sed 's/supported_rasppi=(//' | sed 's/)//'; \
	fi \
)
SUPPORTED_RASPPI_64 := $(shell \
	if [ -f "$(BUILD_CONF)" ]; then \
		grep '^supported_rasppi_64=' "$(BUILD_CONF)" 2>/dev/null | sed 's/supported_rasppi_64=(//' | sed 's/)//'; \
	fi \
)
# Fallback if PREFIX is empty
ifeq ($(PREFIX),)
PREFIX = arm-none-eabi-
endif
# Fallback if PREFIX64 is empty
ifeq ($(PREFIX64),)
PREFIX64 = aarch64-none-elf-
endif
# Determine architecture mode
ARCH_MODE ?= 32
CURRENT_PREFIX = $(if $(filter 64,$(ARCH_MODE)),$(PREFIX64),$(PREFIX))
CURRENT_SUPPORTED = $(if $(filter 64,$(ARCH_MODE)),$(SUPPORTED_RASPPI_64),$(SUPPORTED_RASPPI))
CURRENT_DIST_DIR = $(if $(filter 64,$(ARCH_MODE)),$(DIST_DIR)64,$(DIST_DIR))
CURRENT_ZIP_NAME = $(if $(filter 64,$(ARCH_MODE)),usbode-$(BUILD_VERSION)-$(BRANCH)-$(COMMIT)-64bit.zip,$(ZIP_NAME))

RASPPI ?= $(if $(CURRENT_SUPPORTED),$(word 1,$(CURRENT_SUPPORTED)),1)
# Fallback if empty
ifeq ($(SUPPORTED_RASPPI),)
SUPPORTED_RASPPI = 1 2 3 4
endif
ifeq ($(SUPPORTED_RASPPI_64),)
SUPPORTED_RASPPI_64 = 4
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
	 show-build-info rebuild show-config all-32 all-64 multi-arch-64 package-both

all: clean-all clean-dist configure circle-deps circle-addons usbode-addons kernel dist-files

check-vars:
	@if [ -n "$(RASSPI)" ]; then \
		echo "ERROR: Did you mean RASPPI=$(RASSPI) instead of RASSPI=$(RASSPI)?"; \
		exit 1; \
	fi

check-config:
	@if [ ! -f "$(BUILD_CONF)" ]; then \
		echo "Warning: $(BUILD_CONF) not found, using default PREFIX=$(PREFIX)"; \
	else \
		echo "Using PREFIX=$(CURRENT_PREFIX) from $(BUILD_CONF) ($(ARCH_MODE)-bit mode)"; \
	fi

# Configure Circle for target architecture
configure: check-vars check-config
	@echo "Configuring for RASPPI=$(RASPPI) ($(ARCH_MODE)-bit mode)$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"
	@echo "Using PREFIX=$(CURRENT_PREFIX)"
	git submodule update --init --recursive
	cd $(STDLIBHOME) && \
	rm -rf build && \
	mkdir -p build/circle-newlib && \
	./configure -r $(RASPPI) --prefix "$(CURRENT_PREFIX)" $(DEBUG_CONFIGURE_FLAGS)

# Build Circle stdlib
circle-stdlib: configure
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
	@echo "Creating single-architecture distribution package for RASPPI=$(RASPPI) ($(ARCH_MODE)-bit)..."
	
	# Copy kernel files for current RASPPI architecture
	cp src/kernel*.img $(CURRENT_DIST_DIR)/
	
	# Call the existing dist target (without kernel dependency)
	@$(MAKE) dist-files ARCH_MODE=$(ARCH_MODE)

dist-files:
	@echo "Creating distribution package ($(ARCH_MODE)-bit)..."
	@echo "Platform Specific Builds Complete Successfully, copying general files to $(CURRENT_DIST_DIR)"
	@mkdir -p $(CURRENT_DIST_DIR)
	# Copy configuration files
	cp sdcard/wpa_supplicant.conf $(CURRENT_DIST_DIR)/
	cp sdcard/cmdline.txt $(CURRENT_DIST_DIR)/
	
	# Create and populate images directory
	mkdir -p $(CURRENT_DIST_DIR)/images
	cp sdcard/image.iso.gz $(CURRENT_DIST_DIR)/images/
	gunzip $(CURRENT_DIST_DIR)/images/image.iso.gz
	
	# Create and populate system directory
	mkdir -p $(CURRENT_DIST_DIR)/system
	cp sdcard/test.pcm.gz $(CURRENT_DIST_DIR)/system/
	gunzip $(CURRENT_DIST_DIR)/system/test.pcm.gz
	
	# Create firmware directory
	mkdir -p $(CURRENT_DIST_DIR)/firmware
	
	# Convert CHD files if chdman is available
	@if command -v chdman >/dev/null 2>&1; then \
		echo "Using chdman to convert CHD files"; \
		chdman extractcd -i sdcard/usbode-audio-test.chd -o $(CURRENT_DIST_DIR)/images/usbode-audio-sampler.cue; \
	else \
		echo "chdman not found, skipping CHD conversion"; \
	fi
	
	# Copy firmware files
	cp $(CIRCLEHOME)/addon/wlan/firmware/* $(CURRENT_DIST_DIR)/firmware/
	rm -f $(CURRENT_DIST_DIR)/firmware/Makefile
	
	# Copy boot files
	mkdir -p $(CURRENT_DIST_DIR)/overlays
	cp $(CIRCLEHOME)/boot/bootcode.bin $(CURRENT_DIST_DIR)/
	@for f in start.elf start4.elf; do \
		if [ -f "$(CIRCLEHOME)/boot/$$f" ]; then cp "$(CIRCLEHOME)/boot/$$f" $(CURRENT_DIST_DIR)/; fi; \
	done
	@for f in fixup.dat fixup4.dat; do \
		if [ -f "$(CIRCLEHOME)/boot/$$f" ]; then cp "$(CIRCLEHOME)/boot/$$f" $(CURRENT_DIST_DIR)/; fi; \
	done
	cp $(CIRCLEHOME)/boot/bcm*.dtb $(CURRENT_DIST_DIR)/
	cp $(CIRCLEHOME)/boot/bcm*.dtbo $(CURRENT_DIST_DIR)/overlays
	cp $(CIRCLEHOME)/boot/LICENCE.broadcom $(CURRENT_DIST_DIR)/
	cp $(CIRCLEHOME)/boot/COPYING.linux $(CURRENT_DIST_DIR)/firmware/

	# Create config.txt based on architecture
	@if [ "$(ARCH_MODE)" = "64" ]; then \
		echo "Using 64-bit config"; \
		cp $(CIRCLEHOME)/boot/config64.txt $(CURRENT_DIST_DIR)/config.txt; \
	else \
		echo "Using 32-bit config"; \
		cp $(CIRCLEHOME)/boot/config32.txt $(CURRENT_DIST_DIR)/config.txt; \
	fi
	# Remove problematic lines from pi4 config.txt
	sed -i.bak -e 's/^\(armstub=armstub7-rpi4\.bin\)/#\1/' -e 's/^\(max_framebuffers=2\)/#\1/' $(CURRENT_DIST_DIR)/config.txt && rm $(CURRENT_DIST_DIR)/config.txt.bak
	cat sdcard/config-usbode.txt >> $(CURRENT_DIST_DIR)/config.txt
	cp sdcard/config-options.txt $(CURRENT_DIST_DIR)/
	cp sdcard/cmdline.txt $(CURRENT_DIST_DIR)/
	
	# Create zip file
	@echo "Creating $(CURRENT_ZIP_NAME)..."
	@cd $(CURRENT_DIST_DIR) && zip -r ../$(CURRENT_ZIP_NAME) ./*
	@echo "Built $(CURRENT_ZIP_NAME). Copy the contents of the zip file to a freshly formatted SDCard (FAT32 or EXFAT) and try the build!"

# Clean everything
clean-all:
	@echo "Cleaning all modules..."
	@rm -rf $(DIST_DIR) $(DIST_DIR)64
	@rm -f usbode*.zip
	@cd src && $(MAKE) clean
	@for module in $(USBODE_ADDONS); do \
		echo "Cleaning addon/$$module"; \
		cd addon/$$module && $(MAKE) clean; \
		cd ../..; \
	done
	@for addon in $(CIRCLE_ADDONS); do \
		echo "Cleaning $(CIRCLEHOME)/addon/$$addon"; \
		cd $(CIRCLEHOME)/addon/$$addon && $(MAKE) clean; \
	done
	@cd $(STDLIBHOME) && $(MAKE) clean

# Override Circle's clean to call our clean-all
clean: clean-all

# Clean distribution files and prepare fresh dist directory
clean-dist:
	@echo "Cleaning distribution directory ($(ARCH_MODE)-bit)..."
	@rm -rf $(CURRENT_DIST_DIR)
	@rm -f $(CURRENT_ZIP_NAME)
	@mkdir -p $(CURRENT_DIST_DIR)

# 32-bit specific targets
all-32: 
	@$(MAKE) multi-arch ARCH_MODE=32

# 64-bit specific targets  
all-64: 
	@$(MAKE) multi-arch-64 ARCH_MODE=64

# Multi-architecture build for 32-bit
multi-arch: clean-dist
	@for arch in $(SUPPORTED_RASPPI); do \
		echo "Building for RASPPI=$$arch (32-bit)$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"; \
		if ! $(MAKE) RASPPI=$$arch ARCH_MODE=32 DEBUG_FLAGS="$(DEBUG_FLAGS)" configure circle-deps circle-addons usbode-addons kernel; then \
			echo "ERROR: Build failed for RASPPI=$$arch (32-bit)"; \
			exit 1; \
		fi; \
		if ! cp src/kernel*.img $(CURRENT_DIST_DIR)/; then \
			echo "ERROR: Failed to copy kernel files for RASPPI=$$arch (32-bit)"; \
			exit 1; \
		fi; \
	done
	@$(MAKE) dist-files ARCH_MODE=32

# Multi-architecture build for 64-bit
multi-arch-64: 
	@$(MAKE) clean-dist ARCH_MODE=64
	@for arch in $(SUPPORTED_RASPPI_64); do \
		echo "Building for RASPPI=$$arch (64-bit)$(if $(DEBUG_FLAGS), with debug flags: $(DEBUG_FLAGS))"; \
		if ! $(MAKE) RASPPI=$$arch ARCH_MODE=64 DEBUG_FLAGS="$(DEBUG_FLAGS)" configure circle-deps circle-addons usbode-addons kernel; then \
			echo "ERROR: Build failed for RASPPI=$$arch (64-bit)"; \
			exit 1; \
		fi; \
		if ! cp src/kernel*.img $(CURRENT_DIST_DIR)/; then \
			echo "ERROR: Failed to copy kernel files for RASPPI=$$arch (64-bit)"; \
			exit 1; \
		fi; \
	done
	@$(MAKE) dist-files ARCH_MODE=64

# Build both 32-bit and 64-bit packages
package-both: 
	@echo "Building both 32-bit and 64-bit packages..."
	@$(MAKE) multi-arch
	@$(MAKE) multi-arch-64

package: multi-arch

release: 
	@if [ -z "$(BUILD_NUMBER)" ]; then \
		echo "Error: BUILD_NUMBER not set. Use: make release BUILD_NUMBER=123"; \
		exit 1; \
	fi
	@$(MAKE) package BUILD_NUMBER="$(BUILD_NUMBER)"

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
	@echo "Current ARCH_MODE = $(ARCH_MODE)"
	@echo "Current RASPPI = $(RASPPI)"
	@echo "SUPPORTED_RASPPI = $(SUPPORTED_RASPPI)"
	@echo "SUPPORTED_RASPPI_64 = $(SUPPORTED_RASPPI_64)"
	@echo "PREFIX = $(PREFIX)"
	@echo "PREFIX64 = $(PREFIX64)"
	@echo "CURRENT_PREFIX = $(CURRENT_PREFIX)"
	@echo "DEBUG_FLAGS = $(DEBUG_FLAGS)"
	@echo "CIRCLE_ADDONS = $(CIRCLE_ADDONS)"
	@echo "USBODE_ADDONS = $(USBODE_ADDONS)"
	@echo "USBCDGADGET_CPPFLAGS = $(USBCDGADGET_CPPFLAGS)"
	@echo "CURRENT_DIST_DIR = $(CURRENT_DIST_DIR)"
	@echo "CURRENT_ZIP_NAME = $(CURRENT_ZIP_NAME)"