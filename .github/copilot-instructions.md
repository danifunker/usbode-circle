# USBODE Circle Copilot Instructions

## Project Overview
USBODE (USB Optical Drive Emulator) is a **bare-metal C++ application** running on the Circle framework that turns a Raspberry Pi into a virtual CD-ROM drive. It presents ISO/BIN-CUE disc images to retro computers via USB Mass Storage protocol.

**Key Architecture**: This is NOT a Linux application - it's a bare-metal embedded system using Circle (a C++ bare metal environment for Raspberry Pi).

## Core Components & Service Architecture

### Main Service Modules (`addon/` directory)
- **`usbmsdgadget/`** - USB Mass Storage Device implementation (core functionality)
- **`webserver/`** - HTTP daemon for web interface on port 80
- **`ftpserver/`** - FTP daemon for file transfers on port 21  
- **`displayservice/`** - HAT display management (Waveshare OLED, PirateAudio)
- **`configservice/`** - Configuration file parsing and management
- **`cdplayer/`** - CD audio playback support
- **`discimage/`** - ISO/BIN-CUE image parsing and mounting
- **`filelogdaemon/`** - Background logging service
- **`sdcardservice/`** - SD card partition management

### Critical Build System
```bash
# Multi-architecture build for all supported Pi models
make multi-arch

# Single architecture build (RASPPI=1,2,3,4)
make RASPPI=4 dist-single

# With debug flags
make RASPPI=4 DEBUG_FLAGS="USB_GADGET_DEBUG" dist-single
```

**Configuration**: Copy `build-usbode.conf.example` to `${HOME}/build-usbode.conf` and set:
```
supported_rasppi=(1 2 3 4)
PathPrefix=arm-none-eabi-
```

### Kernel Initialization Pattern
`src/kernel.cpp` follows this service startup sequence:
1. Hardware initialization (screen, serial, interrupts, timer)
2. SD card and file system mounting (`0:/` = boot partition, `1:/` = images)
3. Network setup (WLAN + WPA supplicant)
4. Service task spawning (all services inherit from `CTask`)
5. USB gadget initialization (mass storage device)

## Development Patterns

### Service Implementation
All services inherit from `CTask` and implement the worker pattern:
```cpp
class YourService : public CTask {
    void Run(void) override {
        while (!IsCancelled()) {
            // Service logic
            m_Scheduler.Yield();
        }
    }
};
```

### USB Gadget Development
**Critical**: USB reset/enumeration handling requires Linux kernel compliance patterns:
- Endpoint sequence reset on USB reset events
- Proper stall condition clearing
- MSD protocol state machine reset
- Device address reset to 0 on bus reset

### File System Layout
- `0:/` (boot partition): `config.txt`, `wpa_supplicant.conf`, firmware
- `1:/` (IMGSTORE): ISO/BIN-CUE image files
- Images accessed via `1:/filename.iso` paths
- Web interface shows files from images partition

### Audio Integration
- PWM audio: Built-in, low quality
- HAT audio: PirateAudio HAT provides line-out for CD audio
- Audio test function available via web interface

## Build Debugging

### Common Build Issues
- **Missing toolchain**: Install ARM GNU Toolchain 14.2+ 
- **Permission errors**: Linux image builds need sudo permissions for mount/losetup
- **32-bit vs 64-bit**: Pi Zero W requires 32-bit, others support both

### Debug Flags
```bash
USB_GADGET_DEBUG    # USB protocol debugging
WEBSERVER_DEBUG     # HTTP request tracing  
DISPLAY_DEBUG       # HAT display debugging
```

## Integration Points

### Cross-Service Communication
- **Global state**: DeviceState singleton manages current mounted image
- **Event system**: Services communicate via Circle's task scheduler
- **File sharing**: FatFS used for SD card access across services

### External Dependencies  
- **Circle framework**: Base bare-metal environment (`circle-stdlib/`)
- **FatFS**: File system library for SD card access
- **WPA supplicant**: WiFi authentication (compiled from hostap)
- **LVGL**: Display library for HAT interfaces

### Hardware Abstraction
- **Display**: `displayservice/` abstracts different HAT types
- **USB**: `usbmsdgadget/` implements USB device controller
- **Audio**: Multiple backends (PWM, HAT, VCHIQ)
- **Network**: Built on Circle's network stack

## Common Workflows

### Adding New Image Format Support
1. Extend `discimage/` parsers for new format
2. Update `cdromservice/` for media detection  
3. Modify web interface to list new format
4. Test with `usbmsdgadget/` protocol handling

### HAT Display Integration
1. Implement display driver in `displayservice/`
2. Add page classes inheriting from base page manager
3. Register with `PageManager` for button/navigation handling
4. Update config parsing for HAT detection

**Key Files**: Always check `Makefile` for build dependencies and `src/kernel.cpp` for service initialization order when adding new components.
