//
// usbcdgadget.cpp
//
// CDROM Gadget by Ian Cass, heavily based on
// USB Mass Storage Gadget by Mike Messinides
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FORF A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <assert.h>
#include <cdplayer/cdplayer.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/sysconfig.h>
#include <circle/usb/dwhciregister.h>
#include <usbcdgadget/usbcdgadget.h>
#include <usbcdgadget/usbcdgadgetendpoint.h>
#include <circle/util.h>
#include <math.h>
#include <stddef.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...)  // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)
#define DEFAULT_BLOCKS 16000

const TUSBDeviceDescriptor CUSBCDGadget::s_DeviceDescriptor =
    {
        sizeof(TUSBDeviceDescriptor),
        DESCRIPTOR_DEVICE,
        0x200,  // bcdUSB - USB 2.0 (keeping USB 2.0 since BIOS detection works)
        0,      // bDeviceClass (0 = class defined at interface level - SeaBIOS expectation)
        0,      // bDeviceSubClass
        0,      // bDeviceProtocol
        64,     // bMaxPacketSize0 (USB 2.0 control endpoint size)
        // 0x04da, // Panasonic
        // 0x0d01,	// CDROM
        USB_GADGET_VENDOR_ID,
        USB_GADGET_DEVICE_ID_CD,
        0x0100,   // bcdDevice (v1.0.0, matches Linux gadget)
        1, 2, 3,  // strings (manufacturer, product, serial)
        1         // num configurations
};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorFullSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1,  // bNumInterfaces
            1,
            0,
            0x80,    // bmAttributes (bus-powered, no remote wakeup - USB 2.0 standard)
            500 / 2  // bMaxPower (500mA - standard USB 2.0 power for optical drives)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            0x08, 0x05, 0x50,  // bInterfaceClass, SubClass, Protocol (ATAPI_8070/CD-ROM - SeaBIOS preferred for optical drives)
            //0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol (SCSI transparent - previously used)
            //0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol (ATAPI_8020 - alternative)
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            64,  // wMaxPacketSize
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            64,  // wMaxPacketSize
            0                                                                            // bInterval
        }};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorHighSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1,  // bNumInterfaces
            1,
            0,
            0x80,    // bmAttributes (bus-powered, no remote wakeup - USB 2.0 standard)
            500 / 2  // bMaxPower (500mA - standard USB 2.0 power for optical drives)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            0x08, 0x05, 0x50,  // bInterfaceClass, SubClass, Protocol (ATAPI_8070/CD-ROM - SeaBIOS preferred for optical drives)
            //0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol (SCSI transparent - previously used)
            //0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol (ATAPI_8020 - alternative)
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize - USB 2.0 High Speed maximum for bulk endpoints
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize - USB 2.0 High Speed maximum for bulk endpoints
            0                                                                            // bInterval
        }};

const char* const CUSBCDGadget::s_StringDescriptor[] =
    {
        "\x04\x03\x09\x04",  // Language ID
        "USBODE",
        "USB Optical Disk Emulator",
        "123456789012" // Serial number - 12 chars, conservative for BIOS compatibility
};

// Static variables for BIOS boot protection
boolean CUSBCDGadget::s_GlobalSuspendDisabled = FALSE;

// Static variable for framework suspend control
boolean CUSBCDGadget::s_DisableSuspend = FALSE;

// Device Qualifier Descriptor - required for USB 2.0 dual-speed devices
// Describes what the device would look like if it were operating at the other speed
const TUSBDeviceQualifierDescriptor CUSBCDGadget::s_DeviceQualifierDescriptor =
    {
        sizeof(TUSBDeviceQualifierDescriptor),
        6,      // DESCRIPTOR_DEVICE_QUALIFIER
        0x200,  // bcdUSB - USB 2.0
        0,      // bDeviceClass
        0,      // bDeviceSubClass  
        0,      // bDeviceProtocol
        64,     // bMaxPacketSize0 (same as device descriptor)
        1,      // bNumConfigurations
        0       // bReserved
};

CUSBCDGadget::CUSBCDGadget(CInterruptSystem* pInterruptSystem, boolean isFullSpeed, CCueBinFileDevice* pDevice)
    : CDWUSBGadget(pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr},
      m_nState(TCDState::Init),
      m_CDReady(false),
      // Initialize enhanced state management
      m_nPendingCount(0),
      m_nActiveRequestIndex(0)
{
    m_IsFullSpeed = isFullSpeed;
    
    // Initialize endpoint states
    m_EndpointState[EPOut] = EPState_Invalid;
    m_EndpointState[EPIn] = EPState_Invalid;
    
    // Initialize request queue
    for (unsigned i = 0; i < MAX_PENDING_REQUESTS; i++) {
        m_PendingRequests[i].bActive = FALSE;
        m_PendingRequests[i].pBuffer = nullptr;
        m_PendingRequests[i].nLength = 0;
        m_PendingRequests[i].nCompleted = 0;
        m_PendingRequests[i].nTimeout = 0;
        m_PendingRequests[i].nRetries = 0;
    }
    
    if (pDevice) {
        SetDevice(pDevice);
    }
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "Constructor complete, CD ready=%d", m_CDReady);
}

CUSBCDGadget::~CUSBCDGadget(void) {
    MLOGNOTE("CUSBCDGadget::~CUSBCDGadget", "Destructor called - cleaning up USB CD gadget");
    // Don't crash on destruction - just clean up gracefully
}

const void* CUSBCDGadget::GetDescriptor(u16 wValue, u16 wIndex, size_t* pLength) {
    // Minimal logging during enumeration to avoid timing interference with BIOS
    static u32 boot_attempt_number = 0;
    static u32 last_boot_attempt_time = 0;
    static bool boot_attempt_logged = false;
    
    u32 current_time = CTimer::GetClockTicks();
    
    // Detect new boot attempts (more than 3 seconds since last contact) - log only once per attempt
    if (!boot_attempt_logged || (current_time - last_boot_attempt_time) > (3 * CLOCKHZ)) {
        boot_attempt_number++;
        boot_attempt_logged = true;
        last_boot_attempt_time = current_time;
        // Only log boot attempt number - no verbose logging during enumeration
        MLOGNOTE("CUSBCDGadget::GetDescriptor", "BIOS boot attempt #%u", boot_attempt_number);
    }
    
    // Force device readiness on first contact for BIOS consistency (no logging to avoid timing issues)
    if (boot_attempt_number == 1 && !m_CDReady && m_pDevice) {
        m_CDReady = true;
    }
    
    assert(pLength);

    u8 uchDescIndex = wValue & 0xFF;

    switch (wValue >> 8) {
        case DESCRIPTOR_DEVICE:
            // No verbose logging - timing critical for BIOS enumeration
            if (!uchDescIndex) {
                *pLength = sizeof s_DeviceDescriptor;
                return &s_DeviceDescriptor;
            }
            break;

        case DESCRIPTOR_CONFIGURATION:
            // No verbose logging - timing critical for BIOS enumeration
            if (!uchDescIndex) {
                *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
		return m_IsFullSpeed?&s_ConfigurationDescriptorFullSpeed : &s_ConfigurationDescriptorHighSpeed;
            }
            break;

        case DESCRIPTOR_STRING:
            // No verbose logging - timing critical for BIOS enumeration
            if (!uchDescIndex) {
                *pLength = (u8)s_StringDescriptor[0][0];
                return s_StringDescriptor[0];
            } else if (uchDescIndex < sizeof s_StringDescriptor / sizeof s_StringDescriptor[0]) {
                return ToStringDescriptor(s_StringDescriptor[uchDescIndex], pLength);
            }
            break;

        case 6: // DESCRIPTOR_DEVICE_QUALIFIER
            // No verbose logging - timing critical for BIOS enumeration
            if (!uchDescIndex) {
                *pLength = sizeof s_DeviceQualifierDescriptor;
                return &s_DeviceQualifierDescriptor;
            }
            break;

        case 7: // DESCRIPTOR_OTHER_SPEED_CONFIGURATION  
            // No verbose logging - timing critical for BIOS enumeration
            if (!uchDescIndex) {
                *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
                // Return the opposite speed configuration 
                return m_IsFullSpeed ? &s_ConfigurationDescriptorHighSpeed : &s_ConfigurationDescriptorFullSpeed;
            }
            break;

        default:
            // Only log unknown descriptors for debugging (but keep it minimal)
            if (boot_attempt_number <= 2) {  // Only log for first 2 attempts to avoid spam
                MLOGNOTE("CUSBCDGadget::GetDescriptor", "Unknown descriptor type 0x%02x", wValue >> 8);
            }
            break;
    }

    return nullptr;
}

void CUSBCDGadget::AddEndpoints(void) {
    // Check if endpoints already exist and are valid - if so, preserve them
    if (m_pEP[EPOut] && m_pEP[EPIn] && 
        m_pEP[EPOut]->IsValid() && m_pEP[EPIn]->IsValid()) {
        
        // Update endpoint state tracking
        m_EndpointState[EPOut] = EPState_Ready;
        m_EndpointState[EPIn] = EPState_Ready;
        return;
    }
    
    // Mark endpoints as initializing
    m_EndpointState[EPOut] = EPState_Initializing;
    m_EndpointState[EPIn] = EPState_Initializing;
    
    // Clean up existing endpoints if they exist but are invalid
    if (m_pEP[EPOut] || m_pEP[EPIn]) {
        if (m_pEP[EPOut]) {
            delete m_pEP[EPOut];
            m_pEP[EPOut] = nullptr;
        }
        if (m_pEP[EPIn]) {
            delete m_pEP[EPIn];
            m_pEP[EPIn] = nullptr;
        }
    }
    
    // Flush any pending requests during endpoint recreation
    FlushPendingRequests();
    
    // Reset state during endpoint recreation
    m_nState = TCDState::Init;
    
    // Store previous ready state to restore after endpoint recreation
    boolean wasReady = m_CDReady;
    m_CDReady = false;
    
    // Create OUT endpoint
    assert(!m_pEP[EPOut]);
    if (m_IsFullSpeed)
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorFullSpeed.EndpointOut),
            this);
    else
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorHighSpeed.EndpointOut),
            this);
    assert(m_pEP[EPOut]);

    // Create IN endpoint
    assert(!m_pEP[EPIn]);
    if (m_IsFullSpeed)
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorFullSpeed.EndpointIn),
            this);
    else
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorHighSpeed.EndpointIn),
            this);
    assert(m_pEP[EPIn]);

    // Verify endpoints are properly created and valid
    assert(m_pEP[EPOut]->IsValid());
    assert(m_pEP[EPIn]->IsValid());
    
    // Update endpoint state tracking
    m_EndpointState[EPOut] = EPState_Ready;
    m_EndpointState[EPIn] = EPState_Ready;
    
    // Restore ready state if we had a valid device before endpoint recreation
    if (wasReady && m_pDevice) {
        m_CDReady = true;
        MLOGNOTE("CUSBCDGadget::CreateEndpoints", "*** CD READY STATE RESTORED *** Device available for BIOS boot");
    }
    
    // CRITICAL FIX: Restore saved state if we were in the middle of a BIOS operation
    if (m_StateSaved) {
        MLOGNOTE("CUSBCDGadget::CreateEndpoints", "*** RESTORING CRITICAL STATE *** %s operation for LBA %u after suspend/resume", 
                 (m_SavedState == TCDState::DataInRead) ? "DataInRead" : "Unknown", m_SavedLBA);
        m_nState = m_SavedState;
        m_nblock_address = m_SavedLBA;
        m_nnumber_blocks = m_SavedBlockCount;
        m_StateSaved = false; // Clear the saved state flag
    }
    
    MLOGNOTE("CUSBCDGadget::AddEndpoints", "endpoints created successfully - Speed: %s, suspend/resume cycle #%u", 
        m_IsFullSpeed ? "Full" : "High", m_SuspendResumeCount);

    // ENHANCED BIOS PROTECTION: Check if we interrupted a BIOS boot sequence
    if (m_BioseBootInterrupted && m_BootInterruptTime > 0) {
        u32 current_time = CTimer::GetClockTicks();
        u32 time_since_interrupt = current_time - m_BootInterruptTime;
        
        MLOGNOTE("CUSBCDGadget::AddEndpoints", "BIOS boot recovery - interrupted boot %u seconds ago", 
                 time_since_interrupt / 1000000);
        
        // Clear interrupt flag after recovery - no delays to avoid timing interference
        m_BioseBootInterrupted = false;
        m_BootInterruptTime = 0;
    }

    // CRITICAL FIX: The USB framework is not automatically calling OnActivate()
    // We need to manually trigger activation to start listening for SCSI commands
    MLOGNOTE("CUSBCDGadget::AddEndpoints", "*** MANUAL ACTIVATION TRIGGER *** Framework didn't call OnActivate, calling manually");
    OnActivate();
    
    // State will be set to ReceiveCBW in OnActivate()
}

void CUSBCDGadget::RemoveEndpoints(void) {
    MLOGNOTE("CUSBCDGadget::RemoveEndpoints", "entered");
    
    // Reset state before endpoint cleanup for consistency
    m_nState = TCDState::Init;
    m_CDReady = false;
    
    // Clean up OUT endpoint
    if (m_pEP[EPOut]) {
        MLOGNOTE("CUSBCDGadget::RemoveEndpoints", "removing OUT endpoint");
        delete m_pEP[EPOut];
        m_pEP[EPOut] = nullptr;
    }
    
    // Clean up IN endpoint
    if (m_pEP[EPIn]) {
        MLOGNOTE("CUSBCDGadget::RemoveEndpoints", "removing IN endpoint");
        delete m_pEP[EPIn];
        m_pEP[EPIn] = nullptr;
    }
    
    MLOGNOTE("CUSBCDGadget::RemoveEndpoints", "endpoints cleaned up successfully");
}

// must set device before usb activation
void CUSBCDGadget::SetDevice(CCueBinFileDevice* dev) {
    MLOGNOTE("CUSBCDGadget::SetDevice", "*** CRITICAL TIMING *** SetDevice called with dev=%p, current m_pDevice=%p", dev, m_pDevice);
 
    // Are we changing the device?
    if (m_pDevice && m_pDevice != dev) {
        MLOGNOTE("CUSBCDGadget::SetDevice", "*** DEVICE CHANGE *** Changing device - setting Unit Attention for BIOS");

        delete m_pDevice;
        m_pDevice = nullptr;

        // Tell the host the disc has changed - use same ASC/ASCQ as physical device for BIOS compatibility
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        m_SenseParams.bSenseKey = 0x06;           // Unit Attention (matches physical device)
        m_SenseParams.bAddlSenseCode = 0x29;      // Power On, Reset, Or Bus Device Reset Occurred (matches physical device)
        m_SenseParams.bAddlSenseCodeQual = 0x00;  // (matches physical device)
    }

    m_pDevice = dev;

    // Add null pointer check before using m_pDevice
    if (!m_pDevice) {
        MLOGERR("CUSBCDGadget::SetDevice", "*** ERROR: Device is null! ***");
        return;
    }

    cueParser = CUEParser(m_pDevice->GetCueSheet());  // FIXME. Ensure cuesheet is not null or empty

    MLOGNOTE("CUSBCDGadget::SetDevice", "entered");

    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();

    m_CDReady = true;
    MLOGNOTE("CUSBCDGadget::SetDevice", "*** CD NOW READY FOR BIOS BOOT *** Block size is %d, m_CDReady = %d, device available for SCSI commands", block_size, m_CDReady);

    // Hand the device to the CD Player
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "*** CRITICAL TIMING *** Passed CueBinFileDevice to cd player, device fully ready");
    } else {
        MLOGNOTE("CUSBCDGadget::SetDevice", "*** WARNING *** No cd player found - device still ready for BIOS");
    }
}

int CUSBCDGadget::GetBlocksize() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = cueParser.next_track();
    return GetBlocksizeForTrack(trackInfo);
}

int CUSBCDGadget::GetBlocksizeForTrack(const CUETrackInfo* trackInfo) {
    switch (trackInfo->track_mode) {
        case CUETrack_MODE1_2048:
            MLOGNOTE("CUSBCDGadget::GetBlocksizeForTrack", "CUETrack_MODE1_2048");
            return 2048;
        case CUETrack_MODE1_2352:
            MLOGNOTE("CUSBCDGadget::GetBlocksizeForTrack", "CUETrack_MODE1_2352");
            return 2352;
        case CUETrack_MODE2_2352:
            MLOGNOTE("CUSBCDGadget::GetBlocksizeForTrack", "CUETrack_MODE2_2352");
            return 2352;
        case CUETrack_AUDIO:
            MLOGNOTE("CUSBCDGadget::GetBlocksizeForTrack", "CUETrack_AUDIO");
            return 2352;
        default:
            MLOGERR("CUSBCDGadget::GetBlocksizeForTrack", "Track mode %d not handled", trackInfo->track_mode);
            return 0;
    }
}

int CUSBCDGadget::GetSkipbytes() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = cueParser.next_track();
    return GetSkipbytesForTrack(trackInfo);
}

int CUSBCDGadget::GetSkipbytesForTrack(const CUETrackInfo* trackInfo) {
    switch (trackInfo->track_mode) {
        case CUETrack_MODE1_2048:
            MLOGNOTE("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE1_2048");
            return 0;
        case CUETrack_MODE1_2352:
            MLOGNOTE("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE1_2352");
            return 16;
        case CUETrack_MODE2_2352:
            MLOGNOTE("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE2_2352");
            return 24;
        case CUETrack_AUDIO:
            MLOGNOTE("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_AUDIO");
            return 0;
        default:
            MLOGERR("CUSBCDGadget::GetSkipbytesForTrack", "Track mode %d not handled", trackInfo->track_mode);
            return 0;
    }
}

// Make an assumption about media type based on track 1 mode
int CUSBCDGadget::GetMediumType() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = nullptr;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (trackInfo->track_number == 1 && trackInfo->track_mode == CUETrack_AUDIO)
            // Audio CD
            return 0x02;
        else if (trackInfo->track_number > 1)
            // Mixed mode
            return 0x03;
    }
    // Must be a data cd
    return 0x01;
}

const CUETrackInfo* CUSBCDGadget::GetTrackInfoForLBA(u32 lba) {
    const CUETrackInfo* trackInfo = nullptr;
    int lastTrackNum = 0;
    bool found = false;
    // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Searching for LBA %u", lba);
    cueParser.restart();

    // Shortcut for LBA zero
    if (lba == 0) {
        // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Shortcut lba == 0 returning first track");
        return cueParser.next_track();  // Return the first track
    }

    // Iterate to find our track
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Iterating: Current Track %d data_start is %lu", trackInfo->track_number, trackInfo->data_start);
        //  Shortcut for when our LBA is the start address of this track
        if (trackInfo->data_start == lba) {
            // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Shortcut data_start == lba, returning track %d", trackInfo->track_number);
            return trackInfo;
        }

        if (lba < trackInfo->data_start) {
            // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Found LBA %lu in track %d", lba, lastTrackNum);
            found = true;
            break;
        }

        lastTrackNum = trackInfo->track_number;
    }

    if (found) {
        cueParser.restart();
        while ((trackInfo = cueParser.next_track()) != nullptr) {
            if (trackInfo->track_number == lastTrackNum) {
                // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Returning trackInfo for track %d", trackInfo->track_number);
                return trackInfo;
            }
        }
    } else {
        // We didn't find it, but it might still be in the last track
        if (lba <= GetLeadoutLBA()) {
            // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Shortcut track was last track, returning track %d", trackInfo->track_number);
            return trackInfo;
        }
    }

    // MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "Didn't find LBA %lu", lba);
    //  Not found
    return nullptr;
}

u32 CUSBCDGadget::GetLeadoutLBA() {
    const CUETrackInfo* trackInfo = nullptr;
    u32 file_offset = 0;
    u32 sector_length = 0;
    u32 data_start = 0;

    // Find the last track
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        file_offset = trackInfo->file_offset;
        sector_length = trackInfo->sector_length;
        data_start = trackInfo->data_start;
    }

    u32 deviceSize = 0;
    if (m_pDevice) {
        deviceSize = (u32)m_pDevice->GetSize();
    } else {
        MLOGERR("CUSBCDGadget::GetLeadoutLBA", "*** ERROR: Device is null! ***");
        return 0;
    }

    // We know the start position of the last track, and we know its sector length
    // and we know the file size, so we can work out the LBA of the end of the last track
    // We can't just divide the file size by sector size because sectors lengths might
    // not be consistent (e.g. multi-mode cd where track 1 is 2048
    u32 lastTrackBlocks = (deviceSize - file_offset) / sector_length;
    u32 ret = data_start + lastTrackBlocks;
    MLOGNOTE("CUSBCDGadget::GetLeadoutLBA", "device size is %lu, last track file offset is %lu, last track sector_length is %lu, last track data_start is %lu, lastTrackBlocks = %lu, returning = %lu", deviceSize, file_offset, sector_length, data_start, lastTrackBlocks, ret);

    // Some corrupted cd images might have a cue that references track that are
    // outside the bin.
    if (deviceSize < file_offset)
        return data_start;

    return ret;
}

int CUSBCDGadget::GetLastTrackNumber() {
    const CUETrackInfo* trackInfo = nullptr;
    int lastTrack = 1;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (trackInfo->track_number > lastTrack)
            lastTrack = trackInfo->track_number;
    }
    return lastTrack;
}

void CUSBCDGadget::CreateDevice(void) {
    MLOGNOTE("CUSBCDGadget::GetDescriptor", "entered");
    assert(m_pDevice);
}

void CUSBCDGadget::OnSuspend(void) {
    m_SuspendResumeCount++;
    
    // *** FAST SUSPEND FOR BIOS COMPATIBILITY ***
    // Minimize suspend processing to make it as fast and transparent as possible
    
    // Check if suspend prevention is active
    if (m_PreventSuspend) {
        MLOGNOTE("CUSBCDGadget::OnSuspend", "*** BIOS PROTECTION ACTIVE *** Suspend during protection window, count #%u", m_SuspendResumeCount);
        // Don't disable prevention here - let Update() handle timing
    } else {
        MLOGNOTE("CUSBCDGadget::OnSuspend", "Standard suspend, count #%u", m_SuspendResumeCount);
    }
    
    // CRITICAL: Preserve current state for transparent resume
    if (m_nState == TCDState::DataInRead) {
        // Save critical transfer state
        m_StateSaved = true;
        m_SavedLBA = m_nblock_address;
        m_SavedBlockCount = m_nnumber_blocks;
        m_SavedState = m_nState;
        MLOGNOTE("CUSBCDGadget::OnSuspend", "*** FAST STATE SAVE *** DataInRead LBA=%u blocks=%u", m_SavedLBA, m_SavedBlockCount);
    }
    
    // MINIMAL cleanup for fast resume
    RemoveEndpoints();
    
    MLOGNOTE("CUSBCDGadget::OnSuspend", "*** FAST SUSPEND COMPLETE *** Ready for transparent resume");
}

// CRITICAL BIOS COMPATIBILITY: Handle rapid suspend/resume cycles
void CUSBCDGadget::HandleBIOSStabilization(void) {
    static u32 last_stabilization_time = 0;
    static u32 stabilization_attempts = 0;
    
    u32 current_time = CTimer::GetClockTicks();
    
    // Only attempt stabilization if we've had EXCESSIVE suspend/resume cycles
    // Increase threshold to avoid interfering with normal BIOS enumeration
    if (m_SuspendResumeCount >= 10) {  // Increased from 3 to 10
        // If it's been less than 30 seconds since last stabilization attempt, don't repeat
        if (last_stabilization_time > 0 && (current_time - last_stabilization_time) < (1000000 * 30)) {
            return;
        }
        
        stabilization_attempts++;
        last_stabilization_time = current_time;
        
        MLOGNOTE("CUSBCDGadget::HandleBIOSStabilization", "BIOS stability intervention #%u after %u suspend/resume cycles", 
                 stabilization_attempts, m_SuspendResumeCount);
        
        // Only force reactivation if we're really stuck
        if (m_pEP[EPOut] && m_pEP[EPIn] && m_CDReady && m_nState == TCDState::Init) {
            MLOGNOTE("CUSBCDGadget::HandleBIOSStabilization", "Forcing device back to ReceiveCBW state");
            
            // No delays during BIOS stabilization - they interfere with timing
            
            // Force reactivation - but only if really necessary
            OnActivate();
            
            // Reset suspend/resume counter to prevent excessive interventions
            if (stabilization_attempts >= 2) {  // Reduced attempts
                MLOGNOTE("CUSBCDGadget::HandleBIOSStabilization", "Resetting suspend/resume counter");
                m_SuspendResumeCount = 0;
                stabilization_attempts = 0;
            }
        }
    }
}

const void* CUSBCDGadget::ToStringDescriptor(const char* pString, size_t* pLength) {
    MLOGNOTE("CUSBCDGadget::ToStringDescriptor", "entered");
    assert(pString);

    size_t nLength = 2;
    for (u8* p = m_StringDescriptorBuffer + 2; *pString; pString++) {
        assert(nLength < sizeof m_StringDescriptorBuffer - 1);

        *p++ = (u8)*pString;  // convert to UTF-16
        *p++ = '\0';

        nLength += 2;
    }

    m_StringDescriptorBuffer[0] = (u8)nLength;
    m_StringDescriptorBuffer[1] = DESCRIPTOR_STRING;

    assert(pLength);
    *pLength = nLength;

    return m_StringDescriptorBuffer;
}

int CUSBCDGadget::OnClassOrVendorRequest(const TSetupData* pSetupData, u8* pData) {
    MLOGNOTE("CUSBCDGadget::OnClassOrVendorRequest", "entered");
    if (pSetupData->bmRequestType == 0xA1 && pSetupData->bRequest == 0xfe)  // get max LUN
    {
        MLOGDEBUG("OnClassOrVendorRequest", "state = %i", m_nState);
        pData[0] = 0;
        return 1;
    }
    return -1;
}

void CUSBCDGadget::OnTransferComplete(boolean bIn, size_t nLength) {
    // Enhanced logging for BIOS debugging - track all endpoint activity
    MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** OnTransferComplete entered - state=%d, dir=%s, len=%d", 
             m_nState, bIn ? "IN" : "OUT", nLength);
             
    // This is critical - any OUT transfer when we're in ReceiveCBW state means BIOS is sending SCSI commands
    if (!bIn && m_nState == TCDState::ReceiveCBW) {
        MLOGNOTE("OnTransferComplete", "*** POTENTIAL SCSI COMMAND RECEIVED *** Processing CBW from BIOS");
    }
    
    // Add safety check for endpoint validity during transfer completion
    if (!m_pEP[EPOut] || !m_pEP[EPIn] || 
        !m_pEP[EPOut]->IsValid() || !m_pEP[EPIn]->IsValid()) {
        MLOGERR("OnTransferComplete", "*** ERROR *** endpoints invalid during transfer completion, aborting");
        return;
    }
    
    assert(m_nState != TCDState::Init);
    if (bIn)  // packet to host has been transferred
    {
        MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** Processing IN transfer completion");
        switch (m_nState) {
            case TCDState::SentCSW: {
                MLOGNOTE("OnTransferComplete", "*** CSW SENT SUCCESSFULLY *** - BIOS received command response, now ready for next command");
                
                // Enhanced post-command state management for BIOS compatibility
                static u32 last_completed_command = 0xFF;
                static u32 bios_command_sequence = 0;
                
                if (m_CBW.CBWCB[0] == 0x12) { // INQUIRY command just completed
                    bios_command_sequence++;
                    MLOGNOTE("OnTransferComplete", "*** INQUIRY RESPONSE SENT *** BIOS sequence #%u - expecting TEST UNIT READY next", bios_command_sequence);
                    
                    // Clear any Unit Attention condition - device is now ready
                    m_SenseParams.bSenseKey = 0x00;        // No Sense
                    m_SenseParams.bAddlSenseCode = 0x00;   // No additional sense
                    m_SenseParams.bAddlSenseCodeQual = 0x00;
                    
                    // *** CRITICAL BIOS TIMING *** 
                    // Many BIOSes need a brief moment after INQUIRY before accepting next command
                    // This matches real CD-ROM drive behavior where there's electronic settling time
                    static u32 inquiry_completion_time = 0;
                    inquiry_completion_time = CTimer::GetClockTicks();
                    
                    MLOGNOTE("OnTransferComplete", "*** DEVICE STATE READY *** Unit Attention cleared, ready for operations");
                    MLOGNOTE("OnTransferComplete", "*** BIOS READINESS WINDOW *** Device stable and ready for TEST UNIT READY");
                } else if (m_CBW.CBWCB[0] == 0x00) { // TEST UNIT READY completed
                    MLOGNOTE("OnTransferComplete", "*** TEST UNIT READY SENT *** BIOS should now attempt READ operations");
                }
                
                last_completed_command = m_CBW.CBWCB[0];
                
                m_nState = TCDState::ReceiveCBW;
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** About to call BeginTransfer for next CBW");
                m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut,
                                            m_OutBuffer, SIZE_CBW);
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** BeginTransfer for CBW completed");
                MLOGNOTE("OnTransferComplete", "*** WAITING FOR NEXT BIOS COMMAND *** - if no command comes, BIOS may have enumeration issue");
                break;
            }
            case TCDState::DataIn: {
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** DataIn completion - remaining blocks: %d", m_nnumber_blocks);
                if (m_nnumber_blocks > 0) {
                    if (m_CDReady) {
                        m_nState = TCDState::DataInRead;  // see Update function
                    } else {
                        MLOGERR("onXferCmplt DataIn", "failed, %s",
                                m_CDReady ? "ready" : "not ready");
                        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                        m_SenseParams.bSenseKey = 0x02;
                        m_SenseParams.bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                        m_SenseParams.bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                        MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** About to call SendCSW for DataIn error");
                        SendCSW();
                        MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** SendCSW for DataIn error completed");
                    }
                } else  // done sending data to host
                {
                    MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** DataIn complete, about to send CSW");
                    SendCSW();
                    MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** SendCSW for DataIn completion completed");
                }
                break;
            }
            case TCDState::SendReqSenseReply: {
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** SendReqSenseReply complete, about to send CSW");
                SendCSW();
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** SendCSW for SendReqSenseReply completed");
                break;
            }
            default: {
                MLOGERR("onXferCmplt", "*** ERROR *** dir=in, unhandled state = %i", m_nState);
                // Don't crash - just log error and try to recover
                MLOGERR("onXferCmplt", "*** RECOVERY *** Attempting to recover from unexpected state");
                RecoverFromSCSIException();
                break;
            }
        }
    } else  // packet from host is available in m_OutBuffer
    {
        MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** Processing OUT transfer completion");
        switch (m_nState) {
            case TCDState::ReceiveCBW: {
                if (nLength != SIZE_CBW) {
                    MLOGERR("ReceiveCBW", "*** ERROR *** Invalid CBW len = %i", nLength);
                    m_pEP[EPIn]->StallRequest(true);
                    break;
                }
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** About to copy CBW data");
                memcpy(&m_CBW, m_OutBuffer, SIZE_CBW);
                MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** CBW data copied, checking signature");
                if (m_CBW.dCBWSignature != VALID_CBW_SIG) {
                    MLOGERR("ReceiveCBW", "*** ERROR *** Invalid CBW sig = 0x%x",
                            m_CBW.dCBWSignature);
                    m_pEP[EPIn]->StallRequest(true);
                    break;
                }
                
                // ENHANCED LOGGING: Track CBW reception and command sequence for BIOS debugging
                MLOGNOTE("ReceiveCBW", "*** NEW SCSI COMMAND *** Tag=0x%08x, Length=%u, LUN=%u, CBLength=%u, Command=0x%02x", 
                         m_CBW.dCBWTag, m_CBW.dCBWDataTransferLength, m_CBW.bCBWLUN, m_CBW.bCBWCBLength, m_CBW.CBWCB[0]);
                
                static u32 commandCount = 0;
                commandCount++;
                MLOGNOTE("ReceiveCBW", "BIOS BOOT SEQUENCE: Command #%u - Previous was likely successful if we got here", commandCount);
                
                m_CSW.dCSWTag = m_CBW.dCBWTag;
                m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength; // Initialize to full expected length
                m_CSW.bmCSWStatus = CD_CSW_STATUS_OK; // Initialize to success, commands will override if needed
                if (m_CBW.bCBWCBLength <= 16 && m_CBW.bCBWLUN == 0)  // meaningful CBW
                {
                    MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** About to call HandleSCSICommand");
                    HandleSCSICommand();  // will update m_nstate
                    MLOGNOTE("OnTransferComplete", "*** HANG CHECK *** HandleSCSICommand completed");
                    break;
                }  // TODO: response for not meaningful CBW
                break;
            }

            case TCDState::DataOut: {
                MLOGNOTE("OnXferComplete", "state = %i, dir = %s, len=%i ", m_nState, bIn ? "IN" : "OUT", nLength);
                // process block from host
                // assert(m_nnumber_blocks>0);

                ProcessOut(nLength);

                /*
                if(m_CDReady)
                {
                        m_nState=TCDState::DataOutWrite; //see Update function
                }
                else
                {
                        MLOGERR("onXferCmplt DataOut","failed, %s",
                                m_CDReady?"ready":"not ready");
                        m_CSW.bmCSWStatus=CD_CSW_STATUS_FAIL;
                        m_ReqSenseReply.bSenseKey = 2;
                        m_ReqSenseReply.bAddlSenseCode = 1;
                        SendCSW();
                }
                */
                SendCSW();
                break;
            }

            default: {
                MLOGERR("onXferCmplt", "dir=out, unhandled state = %i", m_nState);
                // Don't crash - just log error and try to recover
                MLOGERR("onXferCmplt", "*** RECOVERY *** Attempting to recover from unexpected OUT state");
                RecoverFromSCSIException();
                break;
            }
        }
    }
}

void CUSBCDGadget::ProcessOut(size_t nLength) {
    // This code is assuming that the payload is a Mode Select payload.
    // At the moment, this is the only thing likely to appear here.
    // TODO: somehow validate what this data is

    MLOGNOTE("ProcessOut",
             "nLength is %d, payload is %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             nLength,
             m_OutBuffer[0], m_OutBuffer[1], m_OutBuffer[2], m_OutBuffer[3],
             m_OutBuffer[4], m_OutBuffer[5], m_OutBuffer[6], m_OutBuffer[7],
             m_OutBuffer[8], m_OutBuffer[9], m_OutBuffer[10], m_OutBuffer[11],
             m_OutBuffer[12], m_OutBuffer[13], m_OutBuffer[14], m_OutBuffer[15],
             m_OutBuffer[16], m_OutBuffer[17], m_OutBuffer[18], m_OutBuffer[19],
             m_OutBuffer[20], m_OutBuffer[21], m_OutBuffer[22], m_OutBuffer[23]);

    // Process our Parameter List
    u8 modePage = m_OutBuffer[9];

    switch (modePage) {
        // CDROM Audio Control Page
        case 0x0e: {
            ModePage0x0EData* modePage = (ModePage0x0EData*)(m_OutBuffer + 8);
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Select (10), Volume is %u,%u", modePage->Output0Volume, modePage->Output1Volume);
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {

		// Descent 2 sets the volume weird. For each volume change, it sends
		// the following in quick succession :-
		// Mode Select (10), Volume is 0,255
		// Mode Select (10), Volume is 255,0
		// Mode Select (10), Volume is 74,255
		// Mode Select (10), Volume is 255,74
		// So, we'll pick Output1Volume which is the last one it sets to a
		// sensible value. Other games seem to set Output0Volume and Output1Volume
		// the same so we should remain compatible

            	MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CDPlayer set volume"); 
                cdplayer->SetVolume(modePage->Output1Volume);
            } else {
            	MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Couldn't get CDPlayer");
	    }
            break;
        }
    }
}

// will be called before vendor request 0xfe
void CUSBCDGadget::OnActivate() {
    // *** FAST ACTIVATION FOR BIOS COMPATIBILITY ***
    // Minimize activation time to ensure rapid, transparent resume
    
    // Quick endpoint check
    if (!m_pEP[EPOut] || !m_pEP[EPIn] || !m_pEP[EPOut]->IsValid() || !m_pEP[EPIn]->IsValid()) {
        MLOGERR("CD OnActivate", "*** FAST ACTIVATION FAILED *** endpoints not ready");
        return;
    }
    
    // *** FAST STATE RESTORATION ***
    if (m_StateSaved && m_SavedState == TCDState::DataInRead) {
        MLOGNOTE("CD OnActivate", "*** FAST RESUME *** DataInRead LBA=%u", m_SavedLBA);
        m_nblock_address = m_SavedLBA;
        m_nnumber_blocks = m_SavedBlockCount;
        m_nState = m_SavedState;
        m_StateSaved = false;
        return; // Let Update() handle the rest
    }
    
    // Standard fast activation
    m_CDReady = true;
    m_nState = TCDState::ReceiveCBW;
    m_StateSaved = false;
    
    // Start CBW receive immediately
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);
    
    MLOGNOTE("CD OnActivate", "*** FAST ACTIVATION COMPLETE *** Ready for BIOS commands");
}

void CUSBCDGadget::SendCSW() {
    MLOGNOTE("CUSBCDGadget::SendCSW", "*** HANG CHECK *** SendCSW entered - about to process response");
    
    if (m_CSW.bmCSWStatus != CD_CSW_STATUS_OK) {
        MLOGERR("CUSBCDGadget::SendCSW", "*** FAILURE RESPONSE *** status: %d (0=OK,1=FAIL), tag: 0x%08x, residue: %d, sense_key: 0x%02x, asc: 0x%02x - THIS MAY CAUSE SeaBIOS ERROR 0003!", 
                 m_CSW.bmCSWStatus, m_CSW.dCSWTag, m_CSW.dCSWDataResidue, m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode);
    } else {
        MLOGNOTE("CUSBCDGadget::SendCSW", "SUCCESS RESPONSE - status: %d, tag: 0x%08x, residue: %d", 
                 m_CSW.bmCSWStatus, m_CSW.dCSWTag, m_CSW.dCSWDataResidue);
    }
    
    MLOGNOTE("CUSBCDGadget::SendCSW", "*** HANG CHECK *** About to memcpy CSW to InBuffer");
    memcpy(&m_InBuffer, &m_CSW, SIZE_CSW);
    
    MLOGNOTE("CUSBCDGadget::SendCSW", "*** HANG CHECK *** About to call BeginTransfer for CSW - CRITICAL HANG POINT");
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCSWIn, m_InBuffer, SIZE_CSW);
    
    MLOGNOTE("CUSBCDGadget::SendCSW", "*** HANG CHECK *** BeginTransfer completed, setting state to SentCSW");
    m_nState = TCDState::SentCSW;
    
    MLOGNOTE("CUSBCDGadget::SendCSW", "*** HANG CHECK *** SendCSW completed successfully");
}

u32 CUSBCDGadget::msf_to_lba(u8 minutes, u8 seconds, u8 frames) {
    // Combine minutes, seconds, and frames into a single LBA-like value
    // The u8 inputs will be promoted to int/u32 for the arithmetic operations
    u32 lba = ((u32)minutes * 60 * 75) + ((u32)seconds * 75) + (u32)frames;

    // Adjust for the 150-frame (2-second) offset.
    lba = lba - 150;

    return lba;
}

u32 CUSBCDGadget::lba_to_msf(u32 lba, boolean relative) {
    if (!relative)
        lba = lba + 150;  // MSF values are offset by 2mins. Weird

    u8 minutes = lba / (75 * 60);
    u8 seconds = (lba / 75) % 60;
    u8 frames = lba % 75;
    u8 reserved = 0;

    return (frames << 24) | (seconds << 16) | (minutes << 8) | reserved;
}

u32 CUSBCDGadget::GetAddress(u32 lba, int msf, boolean relative) {
    u32 address = lba;
    if (msf)
        return lba_to_msf(lba, relative);
    return htonl(address);
}

// TODO: This entire method is a monster. Break up into a Function table of static methods
//
//  Each command lives in its own .cpp file with a class that has a static Handle() function.
//  Then the table contains function pointers to those static methods.
//
//  This will mean we need to tidy up class global variables
//
//  e.g.
//  "inquirycommand.h"
//  class InquiryCommand {
//  public:
//      static void Handle(const uint8_t* cmd, void* context);
//  };
//
//  "inquirycommand.cpp"
//  #include "InquiryCommand.h"
//  void InquiryCommand::Handle(const uint8_t* cmd, void* context) {
//      // Fill m_InBuffer, set status, etc.
//  }
//
//  ... and then the code in here
//  #include "InquiryCommand.h"
//  #include "RequestSenseCommand.h"
//  etc...
//
//  typedef void (*SCSIHandler)(const uint8_t*, void*);
//  SCSIHandler g_SCSIHandlers[256] = {
//      /* initialized below */
//  };
//
//  void InitSCSIHandlers() {
//      g_SCSIHandlers[0x12] = InquiryCommand::Handle;
//      g_SCSIHandlers[0x03] = RequestSenseCommand::Handle;
//      ...
//  }
//
//  https://chatgpt.com/share/683ecad4-e250-8012-b9aa-22c76de6e871
//
// ENHANCED LOGGING FOR BIOS BOOT DIAGNOSIS:
// SeaBIOS error code 0003 = "Could not read from CDROM (code 0003)" 
// This happens when SeaBIOS fails to read Boot Record Volume Descriptor at LBA 0x11
// The BIOS boot sequence is: TEST UNIT READY -> READ(10) LBA 0x11 -> [parse boot catalog]
// Critical LBAs: 0x10=Primary Volume Descriptor, 0x11=Boot Record Volume Descriptor
//
void CUSBCDGadget::HandleSCSICommand() {
    // Track SCSI command timing for BIOS boot protection
    m_LastSCSICommandTime = CTimer::GetClockTicks();
    
    // If boot was previously interrupted, note the recovery
    if (m_BioseBootInterrupted) {
        u32 time_since_interrupt = m_LastSCSICommandTime - m_BootInterruptTime;
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "BIOS boot recovery after %u seconds", time_since_interrupt / 1000000);
        m_BioseBootInterrupted = false; // Clear the flag
    }
    
    static bool bFirstCommand = true;
    static u32 command_count = 0;
    static u32 last_command = 0xFF;
    
    command_count++;
    u32 current_command = m_CBW.CBWCB[0];
    
    if (bFirstCommand) {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "🎯 BIOS CONTACT ESTABLISHED - First SCSI command received");
        bFirstCommand = false;
    }
    
    // Track BIOS boot command sequence for diagnosis - but with minimal logging
    if (command_count <= 10) {  // Only log first 10 commands to avoid spam
        const char* cmd_names[] = {
            "TEST_UNIT_READY", "REZERO_UNIT", "RESERVED", "REQUEST_SENSE", "FORMAT_UNIT", "READ_BLOCK_LIMITS", "RESERVED", "REASSIGN_BLOCKS",
            "READ_6", "RESERVED", "WRITE_6", "SEEK_6", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
            "INQUIRY", "RESERVED", "RECOVER_BUFFERED_DATA", "MODE_SELECT", "RESERVE", "RELEASE", "COPY", "ERASE",
            "MODE_SENSE", "START_STOP", "RECEIVE_DIAGNOSTIC", "SEND_DIAGNOSTIC", "PREVENT_ALLOW_MEDIUM_REMOVAL", "RESERVED", "RESERVED", "RESERVED",
            "READ_FORMAT_CAPACITIES", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
            "READ_10", "RESERVED", "WRITE_10", "SEEK_10", "RESERVED", "RESERVED", "RESERVED", "RESERVED"
        };
        
        const char* cmd_name = (current_command < sizeof(cmd_names)/sizeof(cmd_names[0])) ? cmd_names[current_command] : "UNKNOWN";
        
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "BIOS cmd #%u: 0x%02x (%s)", 
                 command_count, current_command, cmd_name);
                 
        // Track expected BIOS boot progression for diagnosis
        if (command_count == 1 && current_command == 0x12) {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "INQUIRY first - normal BIOS behavior");
        } else if (command_count == 2 && current_command == 0x00) {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "TEST UNIT READY after INQUIRY - good progression");
        } else if (current_command == 0x28 || current_command == 0x08) {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ command - BIOS attempting to boot!");
        }
    }
    
    last_command = current_command;
             
    switch (m_CBW.CBWCB[0]) {
        case 0x0:  // Test unit ready
        {
            // Minimal logging to avoid timing interference during BIOS enumeration
            static int test_unit_ready_count = 0;
            test_unit_ready_count++;
            
            if (test_unit_ready_count <= 3) {  // Only log first 3 TEST UNIT READY commands to avoid spam
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "TEST UNIT READY #%d - CDReady=%d", test_unit_ready_count, m_CDReady);
            }
            
            if (!m_CDReady) {
                if (test_unit_ready_count <= 3) {
                    MLOGERR("CUSBCDGadget::HandleSCSICommand", "*** TEST UNIT READY FAILED *** CD not ready, this will cause BIOS error 0003!");
                    MLOGERR("CUSBCDGadget::HandleSCSICommand", "*** DIAGNOSTIC *** m_CDReady=%d, m_pDevice=%p", m_CDReady, m_pDevice);
                }
                bmCSWStatus = CD_CSW_STATUS_FAIL;
                m_SenseParams.bSenseKey = 0x06;           // Unit Attention (matches physical device)
                m_SenseParams.bAddlSenseCode = 0x29;      // Power On, Reset, Or Bus Device Reset Occurred (matches physical device)
                m_SenseParams.bAddlSenseCodeQual = 0x00;  // (matches physical device)
            } else {
                // TEST UNIT READY success - device is ready for read operations
                if (test_unit_ready_count <= 3) {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** TEST UNIT READY SUCCESS *** Device ready for BIOS read operations");
                }
                bmCSWStatus = CD_CSW_STATUS_OK;
                // Ensure sense data shows no error
                m_SenseParams.bSenseKey = 0x00;       // No Sense
                m_SenseParams.bAddlSenseCode = 0x00;  // No additional sense information
                m_SenseParams.bAddlSenseCodeQual = 0x00;
            }
	    
            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x3:  // Request sense CMD
        {
            // This command is the host asking why the last command generated a check condition
            // We'll clear the reason after we've communicated it. If it's still an issue, we'll
            // throw another Check Condition afterwards
            //bool desc = m_CBW.CBWCB[1] & 0x01;
            u8 blocks = (u8)(m_CBW.CBWCB[4]);

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Request Sense CMD - BIOS requesting error details: length=%d, sense_key=0x%02x, asc=0x%02x, ascq=0x%02x", 
                     blocks, m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);

            u8 length = sizeof(TUSBCDRequestSenseReply);
            if (blocks < length)
                length = blocks;

            m_ReqSenseReply.bSenseKey = m_SenseParams.bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = m_SenseParams.bAddlSenseCode;
            m_ReqSenseReply.bAddlSenseCodeQual = m_SenseParams.bAddlSenseCodeQual;

            memcpy(&m_InBuffer, &m_ReqSenseReply, length);

            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, length);

            m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            m_nState = TCDState::SendReqSenseReply;

            // Reset response params after send
	    bmCSWStatus = CD_CSW_STATUS_OK;
            m_SenseParams.bSenseKey = 0; // NO SENSE
            m_SenseParams.bAddlSenseCode = 0; // NO ADDITIONAL SENSE INFORMATION
            m_SenseParams.bAddlSenseCodeQual = 0; // NO ADDITIONAL SENSE INFORMATION
            break;
        }

        case 0x12:  // Inquiry
        {
            int allocationLength = (m_CBW.CBWCB[3] << 8) | m_CBW.CBWCB[4];
            // Minimal logging to avoid timing interference during BIOS enumeration
            
            if ((m_CBW.CBWCB[1] & 0x01) == 0) {  // EVPD bit is 0: Standard Inquiry
		
		// Set response length
		int datalen = SIZE_INQR;
		if (allocationLength < datalen)
		    datalen = allocationLength;

                // Only log essential INQUIRY success - no verbose data dumps
                static int inquiry_count = 0;
                inquiry_count++;
                if (inquiry_count <= 3) {  // Only log first 3 INQUIRY commands to avoid spam
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "INQUIRY #%d: DevType=0x%02x, Version=0x%02x, RespFormat=0x%02x, Length=%d", 
                             inquiry_count, m_InqReply.bPeriphQualDevType, m_InqReply.bVersion, m_InqReply.bRespDataFormatEtc, datalen);
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "INQUIRY #%d: Vendor='NEC     ', Product='CD-ROM DRIVE    ', Rev='1.0 '", inquiry_count);
                }

                // *** BIOS BOOT PROTECTION *** Start suspend prevention after INQUIRY
                // This gives BIOS time to continue with TEST UNIT READY and READ commands
                m_BiosBootProtectionTime = CTimer::GetClockTicks();
                m_PreventSuspend = true;
                
                // *** FRAMEWORK-LEVEL SUSPEND DISABLE *** 
                // Temporarily disable USB suspend interrupts during critical BIOS boot window
                s_DisableSuspend = TRUE;
                DisableUSBSuspendInterrupt();
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** FRAMEWORK SUSPEND DISABLED *** USB suspend interrupts disabled for BIOS boot compatibility");

                // *** CRITICAL BIOS COMPATIBILITY *** 
                // Clear Unit Attention after first successful INQUIRY - this is essential for BIOS continuation
                // Many BIOSes will not proceed past INQUIRY if device is still in Unit Attention state
                m_SenseParams.bSenseKey = 0x00;       // No Sense - device ready for commands
                m_SenseParams.bAddlSenseCode = 0x00;  // No additional sense information  
                m_SenseParams.bAddlSenseCodeQual = 0x00; // No additional sense code qualifier
                
                // *** ENSURE DEVICE READINESS *** 
                // Force device ready state after successful INQUIRY - critical for TEST UNIT READY to succeed
                if (m_pDevice) {
                    m_CDReady = true;
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** DEVICE FORCED READY *** m_CDReady=true for BIOS TEST UNIT READY compatibility");
                }
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** UNIT ATTENTION CLEARED *** Device now ready for BIOS commands");

                memcpy(&m_InBuffer, &m_InqReply, datalen);
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, datalen);
                m_nState = TCDState::DataIn;
                m_nnumber_blocks = 0;  // nothing more after this send
                m_CSW.bmCSWStatus = bmCSWStatus;
                // Set CSW data residue correctly
                m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength > static_cast<u32>(datalen) ? 
                                       m_CBW.dCBWDataTransferLength - static_cast<u32>(datalen) : 0;
            } else {  // EVPD bit is 1: VPD Inquiry
                // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (VPD Inquiry)");
                u8 vpdPageCode = m_CBW.CBWCB[2];
                switch (vpdPageCode) {
                    case 0x00:  // Supported VPD Pages
                    {
                        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Supported VPD Pages)");

                        u8 SupportedVPDPageReply[] = {
                            0x05,  // Byte 0: Peripheral Device Type (0x05 for Optical Memory Device)
                            0x00,  // Byte 1: Page Code (0x00 for Supported VPD Pages page)
                            0x00,  // Byte 2: Page Length (MSB) - total length of page codes following
                            0x03,  // Byte 3: Page Length (LSB) - 3 supported page codes
                            0x00,  // Byte 4: Supported VPD Page Code: Supported VPD Pages (this page itself)
                            0x80,  // Byte 5: Supported VPD Page Code: Unit Serial Number
                        };
                        
                        // Set response length
                        int datalen = sizeof(SupportedVPDPageReply);
                        if (allocationLength < datalen)
                            datalen = allocationLength;

                        memcpy(&m_InBuffer, &SupportedVPDPageReply, sizeof(SupportedVPDPageReply));
                        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                   m_InBuffer, datalen);
                        m_nState = TCDState::DataIn;
                        m_nnumber_blocks = 0;  // nothing more after this send
                        m_CSW.bmCSWStatus = bmCSWStatus;
                        // Set CSW data residue correctly
                        m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength > static_cast<u32>(datalen) ? 
                                               m_CBW.dCBWDataTransferLength - static_cast<u32>(datalen) : 0;
                        break;
                    }

                    case 0x80:  // Unit Serial Number Page
                    {
                        // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unit Serial number Page)");

                        u8 UnitSerialNumberReply[] = {
                            0x05,  // Byte 0: Peripheral Device Type (Optical Memory Device)
                            0x80,  // Byte 1: Page Code (Unit Serial Number page)
                            0x00,  // Byte 2: Page Length (MSB) - Length of serial number data
                            0x0B,  // Byte 3: Page Length (LSB) - 11 bytes follow
                            // Bytes 4 onwards: The actual serial number
                            'U', 'S', 'B', 'O', 'D', 'E', '0', '0', '0', '0', '1'};

                        // Set response length
                        int datalen = sizeof(UnitSerialNumberReply);
                        if (allocationLength < datalen)
                            datalen = allocationLength;

                        memcpy(&m_InBuffer, &UnitSerialNumberReply, sizeof(UnitSerialNumberReply));
                        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                   m_InBuffer, datalen);
                        m_nState = TCDState::DataIn;
                        m_nnumber_blocks = 0;  // nothing more after this send
                        m_CSW.bmCSWStatus = bmCSWStatus;
                        // Set CSW data residue correctly
                        m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength > static_cast<u32>(datalen) ? 
                                               m_CBW.dCBWDataTransferLength - static_cast<u32>(datalen) : 0;
                        break;
                    }

                    case 0x83: {
                        u8 DeviceIdentificationReply[] = {
                            0x05,  // Byte 0: Peripheral Device Type (Optical Memory Device)
                            0x83,  // Byte 1: Page Code (Device Identification page)
                            0x00,  // Byte 2: Page Length (MSB)
                            0x0B,  // Byte 3: Page Length (LSB) - Total length of all designators combined (11 bytes in this example)

                            // --- Start of First Designator (T10 Vendor ID) ---
                            0x01,  // Byte 4: CODE SET (0x01 = ASCII)
                                   //         PIV (0) + Assoc (0) + Type (0x01 = T10 Vendor ID)
                            0x00,  // Byte 5: PROTOCOL IDENTIFIER (0x00 = SCSI)
                            0x08,  // Byte 6: LENGTH (Length of the identifier data itself - 8 bytes)
                            // Bytes 7-14: IDENTIFIER (Your T10 Vendor ID, padded to 8 bytes)
                            'U', 'S', 'B', 'O', 'D', 'E', ' ', ' '  // "USBODE  " - padded to 8 bytes
                            // --- End of First Designator ---
                        };

                        // Set response length
                        int datalen = sizeof(DeviceIdentificationReply);
                        if (allocationLength < datalen)
                            datalen = allocationLength;

                        memcpy(&m_InBuffer, &DeviceIdentificationReply, sizeof(DeviceIdentificationReply));
                        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                   m_InBuffer, datalen);
                        m_nState = TCDState::DataIn;
                        m_nnumber_blocks = 0;  // nothing more after this send
                        m_CSW.bmCSWStatus = bmCSWStatus;
                        break;
                    }

                    default:  // Unsupported VPD Page
                        // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unsupported Page)");
                        //  m_nState = TCDState::DataIn;
                        m_nnumber_blocks = 0;  // nothing more after this send

                        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
                        m_SenseParams.bSenseKey = 0x05;
                        m_SenseParams.bAddlSenseCode = 0x24;      // Invalid Field
                        m_SenseParams.bAddlSenseCodeQual = 0x00;  // In CDB
                        SendCSW();
                        break;
                }
            }
            break;
        }

        case 0x1B:  // Start/stop unit
        {
            int start = m_CBW.CBWCB[4] & 1;
            int loej = (m_CBW.CBWCB[4] >> 1) & 1;
            // TODO: Emulate a disk eject/load
            // loej Start Action
            // 0    0     Stop the disc - no action for us
            // 0    1     Start the disc - no action for us
            // 1    0     Eject the disc - perhaps we need to throw a check condition?
            // 1    1     Load the disc - perhaps we need to throw a check condition?

            MLOGNOTE("HandleSCSI", "start/stop, start = %d, loej = %d", start, loej);
            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x1E:  // PREVENT ALLOW MEDIUM REMOVAL
        {
            // Lie to the host
            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x25:  // Read Capacity (10))
        {
            u32 lastLBA = GetLeadoutLBA() - 1;
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read Capacity (10) - Last LBA = %u, blocks = %u", lastLBA, lastLBA + 1);
            m_ReadCapReply.nLastBlockAddr = htonl(lastLBA);  // this value is the Start address of last recorded lead-out minus 1
            memcpy(&m_InBuffer, &m_ReadCapReply, SIZE_READCAPREP);
            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, SIZE_READCAPREP);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x28:  // Read (10)
        {
            if (m_CDReady) {
                // Where to start reading (LBA)
                m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

                // Number of blocks to read (LBA)
                m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);

                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read (10) - LBA = %u, blocks = %u [BIOS CRITICAL: LBA 0x11=%u is Boot Record Volume Descriptor for SeaBIOS boot]", m_nblock_address, m_nnumber_blocks, 0x11);

                // Check if LBA is within valid range
                u32 maxLBA = GetLeadoutLBA();
                if (m_nblock_address >= maxLBA) {
                    MLOGERR("CUSBCDGadget::HandleSCSICommand", "Read (10) - LBA %u out of range (max %u) - THIS WILL CAUSE SeaBIOS ERROR 0003!", m_nblock_address, maxLBA);
                    m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                    m_SenseParams.bSenseKey = 0x05;      // ILLEGAL REQUEST
                    m_SenseParams.bAddlSenseCode = 0x21; // LOGICAL BLOCK ADDRESS OUT OF RANGE
                    m_SenseParams.bAddlSenseCodeQual = 0x00;
                    SendCSW();
                    break;
                }

                // Log critical BIOS boot sectors
                if (m_nblock_address == 0x11) {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** BIOS BOOT CRITICAL *** Reading Boot Record Volume Descriptor at LBA 0x11 - SeaBIOS needs this to boot or will show error 0003");
                }
                if (m_nblock_address >= 0x10 && m_nblock_address <= 0x12) {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "*** BIOS BOOT CRITICAL *** Reading boot-related sector LBA %u (0x10=Primary Volume Descriptor, 0x11=Boot Record Volume Descriptor)", m_nblock_address);
                }

                // will be updated if read fails on any block
                m_CSW.bmCSWStatus = bmCSWStatus;

                // Transfer Block Size is the size of data to return to host
                // Block Size and Skip Bytes is worked out from cue sheet
                // For a CDROM, this is always 2048
                transfer_block_size = 2048;
                block_size = data_block_size;  // set at SetDevice
                skip_bytes = data_skip_bytes;  // set at SetDevice;

                m_nbyteCount = m_CBW.dCBWDataTransferLength;

                // What is this?
                if (m_nnumber_blocks == 0) {
                    m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;
                }
                m_nState = TCDState::DataInRead;  // see Update() function
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read (10) setup complete - state set to DataInRead, waiting for Update() to process LBA %u", m_nblock_address);
            } else {
                MLOGERR("handleSCSI Read(10)", "CD NOT READY - failed, %s - THIS WILL CAUSE SeaBIOS ERROR 0003!", m_CDReady ? "ready" : "not ready");
                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                m_SenseParams.bSenseKey = 0x02;
                m_SenseParams.bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                m_SenseParams.bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                SendCSW();
            }
            break;
        }

        case 0xBE:  // READ CD
        {
            if (m_CDReady) {
                // example
                // 0000   1b 00 20 6a 5d d4 82 a9 ff ff 00 00 00 00 09 00   .. j]...........
                // 0010   00 04 00 1b 00 02 03 1f 00 00 00 55 53 42 43 20   ...........USBC
                // 0020   6a 5d d4 00 93 00 00 80 00 0c be 04 00 01 cb 70   j].............p
                // 0030   00 00 10 f0 00 00  00 00 00 00 00 00                     ..........
                //
                // 04 = 100 = expected sector type 1 = cd-da
                // 00 01 cd 70 = LBA
                // 00 00 10 = transfer length (LBA blocks to return)
                // f0 = 1111  1  0  0  1
               
                //    1111 = SYNC, header codes, user data, edc/ecc
                //           Return all data
                //    00 = C2 error (don't fabricate audio)
                //    1 = reserved
                //
                //    For now, we're implementing bare minimum for
                //    audio playback

                // will be updated if read fails on any block
                m_CSW.bmCSWStatus = bmCSWStatus;

                // Where to start reading (LBA)
                m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

                // Number of blocks to read (LBA)
                m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);

                // Expected Sector Type. We can use this to derive
                // sector size and offset in most cases
                int expectedSectorType = (m_CBW.CBWCB[1] & 0x1C) >> 2;
                // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "READ CD for %lu blocks at LBA %lu of type %02x", m_nnumber_blocks, m_nblock_address, expectedSectorType);
                switch (expectedSectorType) {
                    case 0x000: {
                        // All types
                        const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
                        skip_bytes = GetSkipbytesForTrack(trackInfo);
                        block_size = GetBlocksizeForTrack(trackInfo);
                        transfer_block_size = GetBlocksize();
                        break;
                    }
                    case 0x001: {
                        // CD-DA
                        block_size = 2352;
                        transfer_block_size = 2352;
                        skip_bytes = 0;
                        break;
                    }
                    case 0x010: {
                        // Mode 1
                        const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
                        skip_bytes = GetSkipbytesForTrack(trackInfo);
                        block_size = GetBlocksizeForTrack(trackInfo);
                        transfer_block_size = 2048;
                        break;
                    }
                    case 0x011: {
                        // Mode 2 formless
                        skip_bytes = 16;
                        block_size = 2352;
                        transfer_block_size = 2336;
                        break;
                    }
                    case 0x100: {
                        // Mode 2 form 1
                        const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
                        skip_bytes = GetSkipbytesForTrack(trackInfo);
                        block_size = GetBlocksizeForTrack(trackInfo);
                        transfer_block_size = 2048;
                        break;
                    }
                    case 0x101: {
                        // Mode 2 form 2
                        block_size = 2352;
                        skip_bytes = 24;
                        transfer_block_size = 2048;
                        break;
                    }
                    default:
                        // Reserved
                        // Should error here!
                        {
                            const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
                            skip_bytes = GetSkipbytesForTrack(trackInfo);
                            block_size = GetBlocksizeForTrack(trackInfo);
                            transfer_block_size = 2324;
                            break;
                        }
                }

                m_nbyteCount = m_CBW.dCBWDataTransferLength;

                // What is this?
                if (m_nnumber_blocks == 0) {
                    m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;  // fixme?
                }
                m_nState = TCDState::DataInRead;  // see Update() function
            } else {
                MLOGNOTE("handleSCSI READ CD", "failed, %s", m_CDReady ? "ready" : "not ready");
                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                m_SenseParams.bSenseKey = 0x02;
                m_SenseParams.bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                m_SenseParams.bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                SendCSW();
            }
            break;
        }

        case 0x2F:  // Verify, not implemented but don't tell host
        {
            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x43:  // READ TOC/PMA/ATIP
        {
            int msf = (m_CBW.CBWCB[1] >> 1) & 0x01;
            //int format = m_CBW.CBWCB[2] & 0x0f; // TODO implement formats. Currently we assume it's always 0x00
            int startingTrack = m_CBW.CBWCB[6];
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read TOC with msf = %02x, starting track = %d, allocation length = %d, m_CDReady = %d", msf, startingTrack, allocationLength, m_CDReady);

            TUSBTOCData m_TOCData;
            TUSBTOCEntry* tocEntries;
            int numtracks = 0;
            int datalen = 0;


            const CUETrackInfo* trackInfo = nullptr;
            int lastTrackNumber = GetLastTrackNumber();

            // Header
            m_TOCData.FirstTrack = 0x01;
            m_TOCData.LastTrack = lastTrackNumber;
            datalen = SIZE_TOC_DATA;

            // Populate the track entries
            tocEntries = new TUSBTOCEntry[lastTrackNumber + 1];

            int index = 0;
            if (startingTrack != 0xAA) {  // Do we only want the leadout?
                cueParser.restart();
                while ((trackInfo = cueParser.next_track()) != nullptr) {
                    if (trackInfo->track_number < startingTrack)
                        continue;
                    // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "Adding at index %d: track number = %d, data_start = %d, start lba or msf %d", index, trackInfo->track_number, trackInfo->data_start, GetAddress(trackInfo->data_start, msf));
                    tocEntries[index].ADR_Control = 0x14;
                    if (trackInfo->track_mode == CUETrack_AUDIO)
                        tocEntries[index].ADR_Control = 0x10;
                    tocEntries[index].reserved = 0x00;
                    tocEntries[index].TrackNumber = trackInfo->track_number;
                    tocEntries[index].reserved2 = 0x00;
                    tocEntries[index].address = GetAddress(trackInfo->data_start, msf);
                    datalen += SIZE_TOC_ENTRY;
                    numtracks++;
                    index++;
                }
            }

            // Lead-Out LBA
            u32 leadOutLBA = GetLeadoutLBA();
            tocEntries[index].ADR_Control = 0x10;
            tocEntries[index].reserved = 0x00;
            tocEntries[index].TrackNumber = 0xAA;
            tocEntries[index].reserved2 = 0x00;
            tocEntries[index].address = GetAddress(leadOutLBA, msf);
            datalen += SIZE_TOC_ENTRY;
            numtracks++;

            // Copy the TOC header
            m_TOCData.DataLength = htons(datalen - 2);
            memcpy(m_InBuffer, &m_TOCData, SIZE_TOC_DATA);

            // Copy the TOC entries immediately after the header
            memcpy(m_InBuffer + SIZE_TOC_DATA, tocEntries, numtracks * SIZE_TOC_ENTRY);

            // Set response length
            if (allocationLength < datalen)
                datalen = allocationLength;

            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, datalen);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;

            delete[] tocEntries;

            break;
        }

        case 0x42:  // READ SUB-CHANNEL CMD
        {
            unsigned int msf = (m_CBW.CBWCB[1] >> 1) & 0x01;
            //unsigned int subq = (m_CBW.CBWCB[2] >> 6) & 0x01; //TODO We're ignoring subq for now
            unsigned int parameter_list = m_CBW.CBWCB[3];
            // unsigned int track_number = m_CBW.CBWCB[6]; // Ignore track number for now. It's used only for ISRC
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
            int length = 0;

            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42), allocationLength = %d, msf = %u, subq = %u, parameter_list = 0x%02x, track_number = %u", allocationLength, msf, subq, parameter_list, track_number);

            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (!cdplayer) {
                MLOGERR("CUSBCDGadget::HandleSCSICommand", "*** ERROR: CDPlayer not found for READ SUB-CHANNEL ***");
                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL; // Command Failed
                m_SenseParams.bSenseKey = 0x02; // Not Ready
                m_SenseParams.bAddlSenseCode = 0x3A; // Medium not present
                m_SenseParams.bAddlSenseCodeQual = 0x00;
                length = 0;
                break;
            }


	    if (parameter_list == 0x00 )
		    parameter_list = 0x01; // 0x00 is "reserved" so let's assume they want cd info
	    
            switch (parameter_list) {
                // Current Position Data request
                case 0x01: {
                    // Current Position Header
                    TUSBCDSubChannelHeaderReply header;
                    memset(&header, 0, SIZE_SUBCHANNEL_HEADER_REPLY);
                    header.audioStatus = 0x00;  // Audio status not supported
                    header.dataLength = SIZE_SUBCHANNEL_01_DATA_REPLY;

                    // Override audio status by querying the player
                    if (cdplayer) {
                        unsigned int state = cdplayer->GetState();
                        switch (state) {
                            case CCDPlayer::PLAYING:
                                header.audioStatus = 0x11;  // Playing
                                break;
                            case CCDPlayer::PAUSED:
                                header.audioStatus = 0x12;  // Paused
                                break;
                            case CCDPlayer::STOPPED_OK:
                                header.audioStatus = 0x13;  // Stopped without error
                                break;
                            case CCDPlayer::STOPPED_ERROR:
                                header.audioStatus = 0x14;  // Stopped with error
                                break;
                            default:
                                header.audioStatus = 0x15;  // No status to return
                                break;
                        }
                    }

                    // Current Position Data
                    TUSBCDSubChannel01CurrentPositionReply data;
                    memset(&data, 0, SIZE_SUBCHANNEL_01_DATA_REPLY);
                    data.dataFormatCode = 0x01;

                    u32 address = 0;
                    if (cdplayer) {
                        address = cdplayer->GetCurrentAddress();
                        data.absoluteAddress = GetAddress(address, msf);
                        const CUETrackInfo* trackInfo = GetTrackInfoForLBA(address);
                        if (trackInfo) {
                            data.trackNumber = trackInfo->track_number;
                            data.indexNumber = 0x01;  // Assume no pregap. Perhaps we need to handle pregap?
                            data.relativeAddress = GetAddress(address - trackInfo->data_start, msf, true);
                        }
                    }

                    // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42, 0x01) audio_status %02x, trackNumber %d, address %d, absoluteAddress %08x, relativeAddress %08x", header.audioStatus, data.trackNumber, address, data.absoluteAddress, data.relativeAddress);

                    // Determine data lengths
                    length = SIZE_SUBCHANNEL_HEADER_REPLY + SIZE_SUBCHANNEL_01_DATA_REPLY;

                    // Copy the header & Code Page
                    memcpy(m_InBuffer, &header, SIZE_SUBCHANNEL_HEADER_REPLY);
                    memcpy(m_InBuffer + SIZE_SUBCHANNEL_HEADER_REPLY, &data, SIZE_SUBCHANNEL_01_DATA_REPLY);
                    break;
                }

                case 0x02: {
                    // Media Catalog Number (UPC Bar Code)
                    break;
                }

                case 0x03: {
                    // International Standard Recording Code (ISRC)
	    	    // TODO We're ignoring track number because that's only valid here
                    break;
                }

                default: {
                    // TODO Error
                }
            }

            if (allocationLength < length)
                length = allocationLength;

            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, length);

            m_nnumber_blocks = 0;  // nothing more after this send
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;

            break;
        }

        case 0x4A:  // GET EVENT STATUS NOTIFICATION
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification");

            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);

            int length = sizeof(TUSBCDEventStatusReply);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &m_EventStatusReply, length);
            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0xAD:  // READ DISC STRUCTURE
        {
            u16 allocationLength = m_CBW.CBWCB[9] << 8 | (m_CBW.CBWCB[10]);
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read Disc Structure, allocation length is %lu", allocationLength);

            u8 cd_physical_format_response_data[] = {
                // Byte 0-1: Data Length (MSB first, Big-Endian)
                // Value: 0x0012 (18 bytes following this field)
                0x00, 0x12,

                // Byte 2: Reserved
                0x00,

                // Byte 3: Disc Status
                // Bit 7: Reserved
                // Bits 6-0: Disc Status (e.g., 0x00 = empty, 0x01 = appendable, 0x02 = complete)
                // 0x02 typically means a completed CD-ROM session.
                0x02,

                // Byte 4-7: First Track Number in Last Session (MSB first, Big-Endian)
                // 0x00000001 = Track 1 (common for single-session discs)
                0x0, 0x0, 0x0, 0x1,

                // Byte 8-11: Number of Sessions (MSB first, Big-Endian)
                // 0x00000001 = 1 session (common for CD-ROM)
                0x0, 0x0, 0x0, 0x1,

                // Byte 12-15: Last Address of Lead-in (MSB first, Big-Endian, LBA)
                // This will vary, but 0x00000000 is a common default for no lead-in defined here explicitly.
                // Real discs would have a valid LBA.
                0x00, 0x00, 0x00, 0x00,

                // Byte 16-19: Last Address of Lead-out (MSB first, Big-Endian, LBA)
                // This will vary, but 0x00000000 is a common default.
                // Real discs would have a valid LBA.
                0x00, 0x00, 0x00, 0x00};

            // Set response length
            int length = sizeof(cd_physical_format_response_data);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &cd_physical_format_response_data, length);

            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x51:  // READ DISC INFORMATION CMD
        {
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read Disc Information");

            m_DiscInfoReply.last_track_last_session = GetLastTrackNumber();
            u32 leadoutLBA = GetLeadoutLBA();
            m_DiscInfoReply.last_lead_in_start_time = htonl(leadoutLBA);
            m_DiscInfoReply.last_possible_lead_out = htonl(leadoutLBA);

            // Set response length
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            int length = sizeof(TUSBDiscInfoReply);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &m_DiscInfoReply, length);
            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x46:  // Get Configuration
        {
            int rt = m_CBW.CBWCB[1] & 0x03;
            int feature = (m_CBW.CBWCB[2] << 8) | m_CBW.CBWCB[3];
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Configuration with rt = %d, feature 0x%04x, allocLen = %d", rt, feature, allocationLength);

            int dataLength = 0;

            switch (rt) {
                case 0x00:  // All features supported
                case 0x01:  // All current features supported
                {
                    // offset to make space for the header
                    dataLength += sizeof(header);

                    // Copy all features
                    memcpy(m_InBuffer + dataLength, &profile_list, sizeof(profile_list));
                    dataLength += sizeof(profile_list);

                    memcpy(m_InBuffer + dataLength, &cdrom_profile, sizeof(cdrom_profile));
                    dataLength += sizeof(cdrom_profile);

                    memcpy(m_InBuffer + dataLength, &core, sizeof(core));
                    dataLength += sizeof(core);

                    memcpy(m_InBuffer + dataLength, &morphing, sizeof(morphing));
                    dataLength += sizeof(morphing);

                    memcpy(m_InBuffer + dataLength, &mechanism, sizeof(mechanism));
                    dataLength += sizeof(mechanism);

                    memcpy(m_InBuffer + dataLength, &boot, sizeof(boot));
                    dataLength += sizeof(boot);

                    memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
                    dataLength += sizeof(multiread);

                    memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                    dataLength += sizeof(cdread);

                    memcpy(m_InBuffer + dataLength, &powermanagement, sizeof(powermanagement));
                    dataLength += sizeof(powermanagement);

                    memcpy(m_InBuffer + dataLength, &audioplay, sizeof(audioplay));
                    dataLength += sizeof(audioplay);

                    // Finally copy the header
                    header.dataLength = htonl(dataLength - 4);
                    memcpy(m_InBuffer, &header, sizeof(header));

                    break;
                }

                case 0x02:  // starting at the feature requested
                {
                    // Offset for header
                    dataLength += sizeof(header);

                    switch (feature) {
                        case 0x00: {  // Profile list
                            memcpy(m_InBuffer + dataLength, &profile_list, sizeof(profile_list));
                            dataLength += sizeof(profile_list);

                            // and its associated profile
                            memcpy(m_InBuffer + dataLength, &cdrom_profile, sizeof(cdrom_profile));
                            dataLength += sizeof(cdrom_profile);
                        }

                        case 0x01: {  // Core
                            memcpy(m_InBuffer + dataLength, &core, sizeof(core));
                            dataLength += sizeof(core);
                        }

                        case 0x02: {  // Morphing
                            memcpy(m_InBuffer + dataLength, &morphing, sizeof(morphing));
                            dataLength += sizeof(morphing);
                        }

                        case 0x03: {  // Removable Medium
                            memcpy(m_InBuffer + dataLength, &mechanism, sizeof(mechanism));
                            dataLength += sizeof(mechanism);
                        }

                        case 0x108: {  // Boot
                            memcpy(m_InBuffer + dataLength, &boot, sizeof(boot));
                            dataLength += sizeof(boot);
                        }

                        case 0x1d: {  // Multiread
                            memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
                            dataLength += sizeof(multiread);
                        }

                        case 0x1e: {  // CD-Read
                            memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                            dataLength += sizeof(cdread);
                        }

                        case 0x100: {  // Power Management
                            memcpy(m_InBuffer + dataLength, &powermanagement, sizeof(powermanagement));
                            dataLength += sizeof(powermanagement);
                        }

                        case 0x103: {  // Analogue Audio Play
                            memcpy(m_InBuffer + dataLength, &audioplay, sizeof(audioplay));
                            dataLength += sizeof(audioplay);
                        }
                    }

                    // Finally copy the header
                    header.dataLength = htonl(dataLength - 4);
                    memcpy(m_InBuffer, &header, sizeof(header));
                    break;
                }
            }

            // Set response length
            if (allocationLength < dataLength)
                dataLength = allocationLength;

            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, dataLength);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x4B:  // PAUSE/RESUME
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PAUSE/RESUME");
            int resume = m_CBW.CBWCB[8] & 0x01;

            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {
                if (resume)
                    cdplayer->Resume();
                else
                    cdplayer->Pause();
            }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x2B:  // SEEK
        {

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SEEK to LBA %lu", m_nblock_address);

            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {
                cdplayer->Seek(m_nblock_address);
            }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x47:  // PLAY AUDIO MSF
        {
            // Start MSF
            u8 SM = m_CBW.CBWCB[3];
            u8 SS = m_CBW.CBWCB[4];
            u8 SF = m_CBW.CBWCB[5];

            // End MSF
            u8 EM = m_CBW.CBWCB[6];
            u8 ES = m_CBW.CBWCB[7];
            u8 EF = m_CBW.CBWCB[8];

            // Convert MSF to LBA
            u32 start_lba = msf_to_lba(SM, SS, SF);
            u32 end_lba = msf_to_lba(EM, ES, EF);
            int num_blocks = end_lba - start_lba;
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO MSF. Start MSF %d:%d:%d, End MSF: %d:%d:%d, start LBA %u, end LBA %u", SM, SS, SF, EM, ES, EF, start_lba, end_lba);

	    const CUETrackInfo* trackInfo = GetTrackInfoForLBA(start_lba);
	    if (trackInfo->track_mode == CUETrack_AUDIO) {
		    // Play the audio
		    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
		    if (cdplayer) {
			if (start_lba == 0xFFFFFFFF)
				cdplayer->Resume();
			else if (start_lba == end_lba)
				cdplayer->Pause();
			else
				cdplayer->Play(start_lba, num_blocks);
		    }
	    } else {
		   bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
                   m_SenseParams.bSenseKey = 0x05;
                   m_SenseParams.bAddlSenseCode = 0x64;      // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
                   m_SenseParams.bAddlSenseCodeQual = 0x00;  
	    }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x4E:  // STOP / SCAN
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "STOP / SCAN");

                CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
                if (cdplayer) {
                        cdplayer->Pause();
                }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }
        case 0x45:  // PLAY AUDIO (10)
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10)");

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

            // Number of blocks to read (LBA)
            m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);
	    
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

	    // Play the audio, but only if length > 0
	    if (m_nnumber_blocks > 0) {
		    const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
		    if (trackInfo->track_mode == CUETrack_AUDIO) {
			CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
			if (cdplayer) {
			    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Play command sent");
			    if (m_nblock_address == 0xffffffff)
				cdplayer->Resume();
			    else
				cdplayer->Play(m_nblock_address, m_nnumber_blocks);
			}
		    } else {
			bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
			m_SenseParams.bSenseKey = 0x05;
			m_SenseParams.bAddlSenseCode = 0x64;      // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
			m_SenseParams.bAddlSenseCodeQual = 0x00;  
		    }
	    }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0xA5:  // PLAY AUDIO (12)
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12)");

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

            // Number of blocks to read (LBA)
            m_nnumber_blocks = (u32)(m_CBW.CBWCB[6] << 24) | (u32)(m_CBW.CBWCB[7] << 16) | (u32)(m_CBW.CBWCB[8] << 8) | m_CBW.CBWCB[9];

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

	    // Play the audio, but only if length > 0
	    if (m_nnumber_blocks > 0) {
		    const CUETrackInfo* trackInfo = GetTrackInfoForLBA(m_nblock_address);
		    if (trackInfo->track_mode == CUETrack_AUDIO) {
			CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
			if (cdplayer) {
			    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Play command sent");
			    if (m_nblock_address == 0xffffffff)
				cdplayer->Resume();
			    else
				cdplayer->Play(m_nblock_address, m_nnumber_blocks);
			}
		    } else {
			bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
			m_SenseParams.bSenseKey = 0x05;
			m_SenseParams.bAddlSenseCode = 0x64;      // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
			m_SenseParams.bAddlSenseCodeQual = 0x00;  
		    }
	    }

            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }


	//TODO Implement 0x52 READ TRACK INFORMATION

        case 0x55:  // Mode Select (10)
        {
            u16 transferLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Select (10), transferLength is %u", transferLength);

            // Read the data from the host but don't do anything with it (yet!)
            m_nState = TCDState::DataOut;
            m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataOut,
                                        m_OutBuffer, transferLength);

            // Unfortunately the payload doesn't arrive here. Check out the
            // ProcessOut method for payload processing

            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x5a:  // Mode Sense (10)
        {
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10)");

            //int LLBAA = (m_CBW.CBWCB[1] >> 7) & 0x01; // We don't support this
            //int DBD = (m_CBW.CBWCB[1] >> 6) & 0x01; // Nor this
            int page = m_CBW.CBWCB[2] & 0x3F;
            int page_control = (m_CBW.CBWCB[2] >> 6) & 0x03;  // We'll ignore this for now
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) with LLBAA = %d, DBD = %d, page = %02x, allocationLength = %lu", LLBAA, DBD, page, allocationLength);

            int length = SIZE_MODE_SENSE10_HEADER;

	    // We don't support saved values
	    if (page_control == 0x03) {
                        bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
                        m_SenseParams.bSenseKey = 0x05;		  // Illegal Request
                        m_SenseParams.bAddlSenseCode = 0x39;      // Saving parameters not supported
                        m_SenseParams.bAddlSenseCodeQual = 0x00;  
	    } else {
		    switch (page) {
			case 0x01: {
			    // Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)

			    // Define our response
			    ModeSense10Header reply_header;
			    memset(&reply_header, 0, sizeof(reply_header));
			    reply_header.modeDataLength = htons(SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X01 - 2);
			    reply_header.mediumType = GetMediumType();
			    reply_header.deviceSpecificParameter = 0xC0;
			    reply_header.blockDescriptorLength = htonl(0x00000000);

			    // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x01 response - modDataLength = %d, mediumType = 0x%02x", SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2, GetMediumType());

			    // Define our Code Page
			    ModePage0x01Data codepage;
			    memset(&codepage, 0, sizeof(codepage));

			    length += SIZE_MODE_SENSE10_PAGE_0X01;

			    // Copy the header & Code Page
			    memcpy(m_InBuffer, &reply_header, SIZE_MODE_SENSE10_HEADER);
			    memcpy(m_InBuffer + SIZE_MODE_SENSE10_HEADER, &codepage, SIZE_MODE_SENSE10_PAGE_0X01);
			    break;
			}
			case 0x1a: {
			    // Mode Page 0x1A (Power Condition)

			    // Define our response
			    ModeSense10Header reply_header;
			    memset(&reply_header, 0, sizeof(reply_header));
			    reply_header.modeDataLength = htons(SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2);
			    reply_header.mediumType = GetMediumType();
			    reply_header.deviceSpecificParameter = 0xC0;
			    reply_header.blockDescriptorLength = htonl(0x00000000);

			    // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x2a response - modDataLength = %d, mediumType = 0x%02x", SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2, GetMediumType());

			    // Define our Code Page
			    ModePage0x1AData codepage;
			    memset(&codepage, 0, sizeof(codepage));
			    codepage.pageCodeAndPS = 0x1a;
			    codepage.pageLength = 0x0a;
			    length += SIZE_MODE_SENSE10_PAGE_0X1A;

			    // Copy the header & Code Page
			    memcpy(m_InBuffer, &reply_header, SIZE_MODE_SENSE10_HEADER);
			    memcpy(m_InBuffer + SIZE_MODE_SENSE10_HEADER, &codepage, SIZE_MODE_SENSE10_PAGE_0X1A);
			    break;
			}
			case 0x2a: {
			    // Mode Page 0x2A (MM Capabilities and Mechanical Status) Data

			    // Define our response
			    ModeSense10Header reply_header;
			    memset(&reply_header, 0, sizeof(reply_header));
			    reply_header.modeDataLength = htons(SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2);
			    reply_header.mediumType = GetMediumType();
			    reply_header.deviceSpecificParameter = 0xC0;
			    reply_header.blockDescriptorLength = htonl(0x00000000);

			    // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x2a response - modDataLength = %d, mediumType = 0x%02x", SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2, GetMediumType());

			    // Define our Code Page
			    ModePage0x2AData codepage;
			    memset(&codepage, 0, sizeof(codepage));
			    codepage.pageCodeAndPS = 0x2a;
			    codepage.pageLength = 18;
			    codepage.capabilityBits[0] = 0x01;  // Can read CD-R
			    codepage.capabilityBits[1] = 0x00;  // Can't write
			    codepage.capabilityBits[2] = 0x01;  // AudioPlay
			    codepage.capabilityBits[3] = 0x03;  // CD-DA Commands Supported, CD-DA Stream is accurate
			    codepage.capabilityBits[4] = 0x20;  // tray loading mechanism
			    codepage.capabilityBits[5] = 0x00;
			    codepage.maxSpeed = htons(706);  // 4x
			    codepage.numVolumeLevels = htons(0x00ff);
			    codepage.bufferSize = htons(0);
			    codepage.currentSpeed = htons(706);

			    length += SIZE_MODE_SENSE10_PAGE_0X2A;

			    // Copy the header & Code Page
			    memcpy(m_InBuffer, &reply_header, SIZE_MODE_SENSE10_HEADER);
			    memcpy(m_InBuffer + SIZE_MODE_SENSE10_HEADER, &codepage, SIZE_MODE_SENSE10_PAGE_0X2A);
			    break;
			}

			case 0x0e: {
			    // Mode Page 0x0E (CD Audio Control Page)

			    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
			    u8 volume = 0xff;
			    if (cdplayer) {
			        // When we return real volume, games that allow volume control don't send proper volume levels
			        // but when we hard code this to 0xff, everything seems to work fine. Weird.
				//volume = cdplayer->GetVolume();
				volume = 0xff;
			    }

			    // Define our response
			    ModeSense10Header reply_header;
			    memset(&reply_header, 0, sizeof(reply_header));
			    reply_header.modeDataLength = htons(SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X0E - 2);
			    reply_header.mediumType = GetMediumType();
			    reply_header.deviceSpecificParameter = 0xC0;
			    reply_header.blockDescriptorLength = htonl(0x00000000);

			    // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x0e response - modDataLength = %d, mediumType = 0x%02x, volume = 0x%02x", SIZE_MODE_SENSE10_HEADER + SIZE_MODE_SENSE10_PAGE_0X2A - 2, GetMediumType(), volume);

			    // Define our Code Page
			    ModePage0x0EData codepage;
			    memset(&codepage, 0, sizeof(codepage));
			    codepage.pageCodeAndPS = 0x0e;
			    codepage.pageLength = 16;
			    codepage.IMMEDAndSOTC = 0x04;
			    codepage.CDDAOutput0Select = 0x01;  // audio channel 0
			    codepage.Output0Volume = volume;  
			    codepage.CDDAOutput1Select = 0x02;  // audio channel 1
			    codepage.Output1Volume = volume;
			    codepage.CDDAOutput2Select = 0x00;  // none
			    codepage.Output2Volume = 0x00;      // muted
			    codepage.CDDAOutput3Select = 0x00;  // none
			    codepage.Output3Volume = 0x00;      // muted

			    length += SIZE_MODE_SENSE10_PAGE_0X0E;

			    // Copy the header & Code Page
			    memcpy(m_InBuffer, &reply_header, SIZE_MODE_SENSE10_HEADER);
			    memcpy(m_InBuffer + SIZE_MODE_SENSE10_HEADER, &codepage, SIZE_MODE_SENSE10_PAGE_0X0E);
			    break;
			}

			default: {
				// We don't support this code page
				bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
				m_SenseParams.bSenseKey = 0x05;		  // Illegal Request
				m_SenseParams.bAddlSenseCode = 0x24;      // INVALID FIELD IN COMMAND PACKET
				m_SenseParams.bAddlSenseCodeQual = 0x00;  
				break;
			}
		    }
	    }

            // Trim the reply length according to what the host requested
            if (allocationLength < length)
                length = allocationLength;

            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10), Sending response with length %d", length);

            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0xAC:  // GET PERFORMANCE
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "GET PERFORMANCE (0xAC)");

            u8 getPerformanceStub[20] = {
                0x00, 0x00, 0x00, 0x10,  // Header: Length = 16 bytes (descriptor)
                0x00, 0x00, 0x00, 0x00,  // Reserved or Start LBA
                0x00, 0x00, 0x00, 0x00,  // Reserved or End LBA
                0x00, 0x00, 0x00, 0x01,  // Performance metric (e.g. 1x speed)
                0x00, 0x00, 0x00, 0x00   // Additional reserved
            };

            memcpy(m_InBuffer, getPerformanceStub, sizeof(getPerformanceStub));

            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, sizeof(getPerformanceStub));
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;

            break;
        }

        default: {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Unknown SCSI Command is 0x%02x", m_CBW.CBWCB[0]);
            m_SenseParams.bSenseKey = 0x5;  // Illegal/not supported
            m_SenseParams.bAddlSenseCode = 0x20;
            m_SenseParams.bAddlSenseCodeQual = 0x00;
            m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
            SendCSW();
            break;
        }
    }

    // Reset the status
    bmCSWStatus = CD_CSW_STATUS_OK;
}

// Enhanced request management methods (inspired by Linux dwc2/gadget.c)
boolean CUSBCDGadget::QueueTransferRequest(boolean bIn, void* pBuffer, size_t nLength) {
    if (m_nPendingCount >= MAX_PENDING_REQUESTS) {
        MLOGDEBUG("USBCDGadget", "Request queue full, dropping request");
        return FALSE;
    }
    
    TTransferRequest* pRequest = &m_PendingRequests[m_nPendingCount];
    pRequest->pBuffer = pBuffer;
    pRequest->nLength = nLength;
    pRequest->nCompleted = 0;
    pRequest->bActive = TRUE;
    pRequest->bIn = bIn;
    pRequest->nTimeout = 1000; // 1 second timeout
    pRequest->nRetries = 3;
    
    m_nPendingCount++;
    return TRUE;
}

void CUSBCDGadget::ProcessPendingRequests(void) {
    for (unsigned i = 0; i < m_nPendingCount; i++) {
        TTransferRequest* pRequest = &m_PendingRequests[i];
        if (pRequest->bActive) {
            // Process active requests - simplified implementation
            pRequest->nCompleted = pRequest->nLength;
            pRequest->bActive = FALSE;
        }
    }
}

void CUSBCDGadget::CompleteRequest(unsigned nIndex, boolean bSuccess) {
    if (nIndex < m_nPendingCount) {
        TTransferRequest* pRequest = &m_PendingRequests[nIndex];
        pRequest->bActive = FALSE;
        if (!bSuccess && pRequest->nRetries > 0) {
            pRequest->nRetries--;
            pRequest->bActive = TRUE; // Retry
        }
    }
}

void CUSBCDGadget::FlushPendingRequests(void) {
    MLOGDEBUG("USBCDGadget", "Flushing %u pending requests", m_nPendingCount);
    
    // Mark all requests as inactive
    for (unsigned i = 0; i < m_nPendingCount; i++) {
        m_PendingRequests[i].bActive = FALSE;
    }
    
    m_nPendingCount = 0;
    m_nActiveRequestIndex = 0;
}

// Enhanced suspend/resume handling (inspired by Linux f_mass_storage.c)
void CUSBCDGadget::PrepareForSuspend(void) {
    // Save current state and prepare for suspend
    m_PreSuspendState = m_nState;
    FlushPendingRequests();
}

boolean CUSBCDGadget::RestoreFromSuspend(void) {
    // Restore state after suspend
    if (m_PreSuspendState != TCDState::Init) {
        m_nState = m_PreSuspendState;
        return TRUE;
    }
    return FALSE;
}

boolean CUSBCDGadget::ValidateEndpointState(void) {
    // Check if endpoints are in valid state
    for (unsigned i = 0; i < NumEPs; i++) {
        if (m_EndpointState[i] == EPState_Invalid) {
            MLOGERR("USBCDGadget", "Endpoint %u in invalid state", i);
            return FALSE;
        }
    }
    return TRUE;
}

// SCSI command state recovery (inspired by Linux exception handling)
void CUSBCDGadget::RecoverFromSCSIException(void) {
    MLOGERR("USBCDGadget", "Recovering from SCSI exception in state %u", (unsigned)m_nState);
    
    // Reset to safe state and prepare CSW with error status
    m_nState = TCDState::DataIn; // Safe state for error recovery
    m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
    m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength;
    
    // Set sense data for exception
    m_SenseParams.bSenseKey = 0x04; // Hardware Error
    m_SenseParams.bAddlSenseCode = 0x44; // Internal target failure
    m_SenseParams.bAddlSenseCodeQual = 0x00;
    
    FlushPendingRequests();
}

boolean CUSBCDGadget::IsTransferSafe(void) {
    // Check if it's safe to perform transfers
    return (m_nState != TCDState::Init && 
            ValidateEndpointState());
}

// Add atomic state transitions to prevent race conditions
boolean CUSBCDGadget::AtomicStateTransition(TCDState fromState, TCDState toState) {
    // Simple atomic state transition - in a real system this would use proper locking
    if (m_nState == fromState) {
        TCDState oldState = m_nState;
        m_nState = toState;
        MLOGDEBUG("USBCDGadget", "State transition: %u -> %u", (unsigned)oldState, (unsigned)toState);
        return TRUE;
    }
    
    MLOGERR("USBCDGadget", "Failed state transition: expected %u, current %u", 
                (unsigned)fromState, (unsigned)m_nState);
    return FALSE;
}

void CUSBCDGadget::ForceStateReset(void) {
    MLOGERR("USBCDGadget", "Force resetting gadget state from %u", (unsigned)m_nState);
    
    // Emergency reset - force to known good state
    m_nState = TCDState::ReceiveCBW;
    
    // Reset all endpoints to ready state
    for (unsigned i = 0; i < NumEPs; i++) {
        m_EndpointState[i] = EPState_Ready;
    }
    
    // Clear any pending transfers
    FlushPendingRequests();
    
    // Reset CSW to clean state
    m_CSW.dCSWSignature = CSW_SIG;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    m_CSW.dCSWDataResidue = 0;
    
    MLOGNOTE("USBCDGadget", "Force state reset completed - ready for new commands");
}

// USB Framework Suspend Control for BIOS compatibility
void CUSBCDGadget::DisableUSBSuspendInterrupt() {
    // Set static flag to disable suspend interrupt handling
    s_DisableSuspend = TRUE;
    MLOGDEBUG("USBCDGadget", "USB suspend interrupt disabled for BIOS compatibility");
}

void CUSBCDGadget::EnableUSBSuspendInterrupt() {
    // Clear static flag to re-enable suspend interrupt handling
    s_DisableSuspend = FALSE;
    MLOGDEBUG("USBCDGadget", "USB suspend interrupt re-enabled");
}

void CUSBCDGadget::Update(void) {
    // Enhanced Update method with suspend prevention and state management
    
    // Handle suspend prevention during BIOS boot sequence
    if (m_PreventSuspend) {
        u32 current_time = CTimer::GetClockTicks();
        if (current_time > m_BiosBootProtectionTime) {
            // BIOS boot protection window has expired
            m_PreventSuspend = FALSE;
            EnableUSBSuspendInterrupt();
            MLOGNOTE("USBCDGadget", "BIOS boot protection window expired - suspend re-enabled");
        } else {
            // Still in protection window - keep device active with minimal logging
            static u32 activity_counter = 0;
            activity_counter++;
            if ((activity_counter % 100000) == 0) {  // Reduced frequency
                u32 remaining_ms = (m_BiosBootProtectionTime - current_time) / 1000;
                MLOGNOTE("USBCDGadget", "BIOS protection active (%u ms remaining) - state: %u", remaining_ms, (unsigned)m_nState);
            }
        }
    }
    
    // Enhanced BIOS diagnostics - track time since last command
    static u32 last_command_time = 0;
    static u32 diagnostic_counter = 0;
    
    diagnostic_counter++;
    if ((diagnostic_counter % 1000000) == 0) {  // Every ~10 seconds
        u32 current_time = CTimer::GetClockTicks();
        if (m_nState == TCDState::ReceiveCBW) {
            if (last_command_time > 0) {
                u32 idle_time_ms = (current_time - last_command_time) / 1000;
                if (idle_time_ms > 5000) {  // 5+ seconds without commands
                    MLOGNOTE("USBCDGadget", "*** BIOS DIAGNOSTIC *** No commands for %u ms - BIOS may have given up", idle_time_ms);
                    MLOGNOTE("USBCDGadget", "*** BIOS DIAGNOSTIC *** Current state: %u, Suspend prevention: %s", 
                             (unsigned)m_nState, m_PreventSuspend ? "ACTIVE" : "inactive");
                }
            }
        } else {
            last_command_time = current_time;  // Update when processing commands
        }
    }
    
    // Validate endpoint states periodically (reduced frequency)
    static u32 validation_counter = 0;
    validation_counter++;
    if ((validation_counter % 100000) == 0) {  // Check every 100000 calls
        if (!ValidateEndpointState()) {
            MLOGERR("USBCDGadget", "Endpoint validation failed - attempting recovery");
            RecoverFromSCSIException();
        }
    }
    
    // Process any pending transfer requests
    ProcessPendingRequests();
}
