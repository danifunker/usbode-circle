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
#include <circle/new.h>
#include <scsitbservice/scsitbservice.h>
#include <cdplayer/cdplayer.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/sysconfig.h>
#include <usbcdgadget/usbcdgadget.h>
#include <usbcdgadget/usbcdgadgetendpoint.h>
#include <circle/util.h>
#include <math.h>
#include <stddef.h>
#include <filesystem>
#include <circle/bcmpropertytags.h>
#include <circle/synchronize.h>

#include <usbcdgadget/cd_utils.h>
#include <usbcdgadget/scsi_inquiry.h>
#include <usbcdgadget/scsi_read.h>
#include <usbcdgadget/scsi_toc.h>
#include <usbcdgadget/scsi_toolbox.h>
#include <usbcdgadget/scsi_misc.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

// Conditional debug logging macro - only logs if m_bDebugLogging is enabled
#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (m_bDebugLogging)             \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

#define DEFAULT_BLOCKS 16000

TUSBDeviceDescriptor CUSBCDGadget::s_DeviceDescriptor =
    {
        sizeof(TUSBDeviceDescriptor),
        DESCRIPTOR_DEVICE,
        0x200, // bcdUSB
        0,     // bDeviceClass
        0,     // bDeviceSubClass
        0,     // bDeviceProtocol
        64,    // bMaxPacketSize0
        // 0x04da, // Panasonic
        // 0x0d01,	// CDROM
        USB_GADGET_VENDOR_ID,
        USB_GADGET_DEVICE_ID_CD,
        0x000,   // bcdDevice
        1, 2, 3, // strings
        1        // num configurations
};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorFullSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1, // bNumInterfaces
            1,
            0,
            0xa0,   // bmAttributes (bus-powered)
            500 / 2 // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                // bInterfaceNumber
            0,                // bAlternateSetting
            2,                // bNumEndpoints
            0x08, 0x02, 0x50, // bInterfaceClass, SubClass, Protocol
            // 0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol
            0 // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81, // IN number 1
            2,    // bmAttributes (Bulk)
            64,   // wMaxPacketSize
            0     // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02, // OUT number 2
            2,    // bmAttributes (Bulk)
            64,   // wMaxPacketSize
            0     // bInterval
        }};

// Apple-specific FullSpeed descriptor (SCSI transparent subclass 0x06)
const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorMacOS9 =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1, // bNumInterfaces
            1,
            0,
            0x80,   // bmAttributes (bus-powered)
            500 / 2 // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                // bInterfaceNumber
            0,                // bAlternateSetting
            2,                // bNumEndpoints
            0x08, 0x06, 0x50, // SubClass 0x06 (SCSI transparent) for Apple
            0                 // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81, // IN number 1
            2,    // bmAttributes (Bulk)
            64,   // wMaxPacketSize
            0     // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02, // OUT number 2
            2,    // bmAttributes (Bulk)
            64,   // wMaxPacketSize
            0     // bInterval
        }};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorHighSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1, // bNumInterfaces
            1,
            0,
            0xa0,   // bmAttributes (bus-powered)
            500 / 2 // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                // bInterfaceNumber
            0,                // bAlternateSetting
            2,                // bNumEndpoints
            0x08, 0x02, 0x50, // bInterfaceClass, SubClass, Protocol
            // 0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol
            0 // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81, // IN number 1
            2,    // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0     // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02, // OUT number 2
            2,    // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0     // bInterval
        }};

const char *const CUSBCDGadget::s_StringDescriptorTemplate[] =
    {
        "\x04\x03\x09\x04", // Language ID
        "USBODE",
        "USB Optical Disk Emulator", // Product (index 2)
        "USBODE00001"                // Template Serial Number (index 3) - will be replaced with hardware serial
};

CUSBCDGadget::CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed,
                           IImageDevice *pDevice, u16 usVendorId, u16 usProductId)
    : CDWUSBGadget(pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
      m_bNeedsAudioInit(FALSE),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr}
{
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget",
             "=== CONSTRUCTOR === pDevice=%p, isFullSpeed=%d", pDevice, isFullSpeed);
    m_IsFullSpeed = isFullSpeed;
    s_DeviceDescriptor.idVendor = usVendorId;
    s_DeviceDescriptor.idProduct = usProductId;
    // Fetch hardware serial number for unique USB device identification
    CBcmPropertyTags Tags;
    TPropertyTagSerial Serial;
    if (Tags.GetTag(PROPTAG_GET_BOARD_SERIAL, &Serial, sizeof(Serial)))
    {
        // Format hardware serial number as "USBODE-XXXXXXXX" using the lower 32 bits
        snprintf(m_HardwareSerialNumber, sizeof(m_HardwareSerialNumber), "USBODE-%08X", Serial.Serial[0]);
        MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "Using hardware serial: %s (from %08X%08X)",
                 m_HardwareSerialNumber, Serial.Serial[1], Serial.Serial[0]);
    }
    else
    {
        // Fallback to default serial number if hardware fetch fails
        strcpy(m_HardwareSerialNumber, "USBODE-00000001");
        MLOGERR("CUSBCDGadget::CUSBCDGadget", "Failed to get hardware serial, using fallback: %s", m_HardwareSerialNumber);
    }

    // Initialize string descriptors with hardware serial number
    m_StringDescriptor[0] = s_StringDescriptorTemplate[0]; // Language ID
    m_StringDescriptor[1] = s_StringDescriptorTemplate[1]; // Manufacturer
    m_StringDescriptor[2] = s_StringDescriptorTemplate[2]; // Product
    m_StringDescriptor[3] = m_HardwareSerialNumber;        // Hardware-based serial number

    ConfigService *configService = (ConfigService *)CScheduler::Get()->GetTask("configservice");
    if (configService)
    {
        m_bDebugLogging = configService->GetProperty("debug_cdrom", 0U) != 0;
        if (m_bDebugLogging)
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget", "CD-ROM debug logging enabled");
        }

        // Get target OS for platform-specific handling
        m_USBTargetOS = configService->GetUSBTargetOS();

        if (m_bDebugLogging)
        {
            const char *osName = (m_USBTargetOS == USBTargetOS::Apple) ? "apple" : "doswin";
            CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget", "Target OS set to: %s", osName);
        }
    }
    else
    {
        m_bDebugLogging = false; // Default to disabled if config service not available
        m_USBTargetOS = USBTargetOS::DosWin;
    }

    // Initialize SCSI Handlers
    InitSCSIHandlers();

    if (pDevice)
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget",
                        "Constructor calling SetDevice()...");

        SetDevice(pDevice);
    }
    else
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget",
                        "Constructor: No initial device provided");
    }
    CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget",
                    "=== CONSTRUCTOR EXIT === m_CDReady=%d, mediaState=%d",
                    m_CDReady, (int)m_mediaState);
}

CUSBCDGadget::~CUSBCDGadget(void)
{
    assert(0);
}

void CUSBCDGadget::InitSCSIHandlers()
{
    // Initialize all to nullptr or a default handler
    for (int i = 0; i < 256; i++)
    {
        m_SCSIHandlers[i] = nullptr;
    }

    // Inquiry & Mode Sense
    m_SCSIHandlers[0x12] = SCSIInquiry::Inquiry;
    m_SCSIHandlers[0x03] = SCSIInquiry::RequestSense;
    m_SCSIHandlers[0x1A] = SCSIInquiry::ModeSense6;
    m_SCSIHandlers[0x5A] = SCSIInquiry::ModeSense10;
    m_SCSIHandlers[0x55] = SCSIInquiry::ModeSelect10;
    m_SCSIHandlers[0x46] = SCSIInquiry::GetConfiguration;

    // Read & Play
    m_SCSIHandlers[0x28] = SCSIRead::Read10;
    m_SCSIHandlers[0xA8] = SCSIRead::Read12;
    m_SCSIHandlers[0x45] = SCSIRead::PlayAudio10;
    m_SCSIHandlers[0xA5] = SCSIRead::PlayAudio12;
    m_SCSIHandlers[0x47] = SCSIRead::PlayAudioMSF;
    m_SCSIHandlers[0x2B] = SCSIRead::Seek;
    m_SCSIHandlers[0x4B] = SCSIRead::PauseResume;
    m_SCSIHandlers[0x4E] = SCSIRead::StopScan;
    m_SCSIHandlers[0xBE] = SCSIRead::ReadCD;

    // TOC & Track Info
    m_SCSIHandlers[0x43] = SCSITOC::ReadTOC;
    m_SCSIHandlers[0x51] = SCSITOC::ReadDiscInformation;
    m_SCSIHandlers[0x52] = SCSITOC::ReadTrackInformation;
    m_SCSIHandlers[0x44] = SCSITOC::ReadHeader;
    m_SCSIHandlers[0x42] = SCSITOC::ReadSubChannel;
    m_SCSIHandlers[0xAD] = SCSITOC::ReadDiscStructure;

    // Toolbox
    m_SCSIHandlers[0xD9] = SCSIToolbox::ListDevices;
    m_SCSIHandlers[0xD2] = SCSIToolbox::NumberOfFiles;
    m_SCSIHandlers[0xDA] = SCSIToolbox::NumberOfFiles; // Same implementation
    m_SCSIHandlers[0xD0] = SCSIToolbox::ListFiles;
    m_SCSIHandlers[0xD7] = SCSIToolbox::ListFiles; // Same implementation
    m_SCSIHandlers[0xD8] = SCSIToolbox::SetNextCD;

    // Misc
    m_SCSIHandlers[0x00] = SCSIMisc::TestUnitReady;
    m_SCSIHandlers[0x1B] = SCSIMisc::StartStopUnit;
    m_SCSIHandlers[0x1E] = SCSIMisc::PreventAllowMediumRemoval;
    m_SCSIHandlers[0x25] = SCSIMisc::ReadCapacity;
    m_SCSIHandlers[0xBD] = SCSIMisc::MechanismStatus;
    m_SCSIHandlers[0x4A] = SCSIMisc::GetEventStatusNotification;
    m_SCSIHandlers[0xAC] = SCSIMisc::GetPerformance;
    m_SCSIHandlers[0xA4] = SCSIMisc::CommandA4;
    m_SCSIHandlers[0x2F] = SCSIMisc::Verify;
    m_SCSIHandlers[0xBB] = SCSIMisc::SetCDROMSpeed;
}

const void *CUSBCDGadget::GetDescriptor(u16 wValue, u16 wIndex, size_t *pLength)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "entered");
    assert(pLength);

    u8 uchDescIndex = wValue & 0xFF;

    switch (wValue >> 8)
    {
    case DESCRIPTOR_DEVICE:
        CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_DEVICE %02x", uchDescIndex);
        if (!uchDescIndex)
        {
            // Use runtime VID/PID from base class members
            static TUSBDeviceDescriptor DeviceDesc = s_DeviceDescriptor;
            *pLength = sizeof DeviceDesc;
            return &DeviceDesc;
        }
        break;

    case DESCRIPTOR_CONFIGURATION:
        CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_CONFIGURATION %02x", uchDescIndex);
        if (!uchDescIndex)
        {
            *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
            if (m_USBTargetOS == USBTargetOS::Apple)
            {
                return &s_ConfigurationDescriptorMacOS9;
            }

            return m_IsFullSpeed ? &s_ConfigurationDescriptorFullSpeed : &s_ConfigurationDescriptorHighSpeed;
        }
        break;

    case DESCRIPTOR_STRING:
        // String descriptors - log for debugging
        if (!uchDescIndex)
        {
            *pLength = (u8)m_StringDescriptor[0][0];
            return m_StringDescriptor[0];
        }
        else if (uchDescIndex < 4)
        { // We have 4 string descriptors (0-3)

            switch (uchDescIndex)
            {
            case 1:
                desc_name = "Manufacturer";
                break;
            case 2:
                desc_name = "Product";
                break;
            case 3:
                desc_name = "Serial Number";
                break;
            default:
                desc_name = "Unknown";
                break;
            }
            return ToStringDescriptor(m_StringDescriptor[uchDescIndex], pLength);
        }
        break;

    default:
        break;
    }

    return nullptr;
}

void CUSBCDGadget::AddEndpoints(void)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::AddEndpoints", "entered");

    assert(!m_pEP[EPOut]);
    assert(!m_pEP[EPIn]);

    // Determine which descriptor set to use
    TDeviceSpeed negotiatedSpeed = GetSpeed();
    if (negotiatedSpeed != UnknownSpeed)
    {
        m_IsFullSpeed = (negotiatedSpeed == FullSpeed);
        MLOGNOTE("CUSBCDGadget::AddEndpoints", 
                 "Negotiated speed: %s (updating m_IsFullSpeed=%d)",
                 m_IsFullSpeed ? "Full-Speed" : "High-Speed", m_IsFullSpeed);
    }
    else
    {
        MLOGNOTE("CUSBCDGadget::AddEndpoints", 
                 "Speed unknown, using initial setting: m_IsFullSpeed=%d", m_IsFullSpeed);
    }

    const TUSBMSTGadgetConfigurationDescriptor *configDesc;

    if (m_USBTargetOS == USBTargetOS::Apple)
    {
        // Apple mode: always use Mac OS 9 descriptors (USB 1.1)
        MLOGNOTE("CUSBCDGadget::AddEndpoints", "Using Mac OS 9 descriptors");
        configDesc = &s_ConfigurationDescriptorMacOS9;
    }
    else if (m_IsFullSpeed || negotiatedSpeed == FullSpeed)
    {
        // Standard full-speed mode
        configDesc = &s_ConfigurationDescriptorFullSpeed;
    }
    else
    {
        // High-speed mode
        configDesc = &s_ConfigurationDescriptorHighSpeed;
    }

    // Create endpoints using selected descriptor
    m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
        reinterpret_cast<const TUSBEndpointDescriptor *>(&configDesc->EndpointOut),
        this);
    assert(m_pEP[EPOut]);

    m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
        reinterpret_cast<const TUSBEndpointDescriptor *>(&configDesc->EndpointIn),
        this);
    assert(m_pEP[EPIn]);

    m_nState = TCDState::Init;
}

// must set device before usb activation
void CUSBCDGadget::SetDevice(IImageDevice *dev)
{
    MLOGNOTE("CUSBCDGadget::SetDevice",
             "=== ENTRY === dev=%p, m_pDevice=%p, m_nState=%d",
             dev, m_pDevice, (int)m_nState);

    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "Passed CueBinFileDevice to cd player");
    }

    boolean bDiscSwap = (m_pDevice != nullptr && m_pDevice != dev);

    if (bDiscSwap || !m_CDReady)
    {
        MLOGNOTE("CUSBCDGadget::SetDevice", "Disc swap detected - ejecting old media");
        delete m_pDevice;
        m_pDevice = nullptr;

        m_CDReady = false;
        m_mediaState = MediaState::NO_MEDIUM;
        m_SenseParams.bSenseKey = 0x02;
        m_SenseParams.bAddlSenseCode = 0x3a;
        m_SenseParams.bAddlSenseCodeQual = 0x00;
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        discChanged = true;

        MLOGNOTE("CUSBCDGadget::SetDevice", "Media ejected: state=NO_MEDIUM, sense=02/3a/00");
    }

    m_pDevice = dev;
    m_mediaType = m_pDevice->GetMediaType();
    MLOGNOTE("CUSBCDGadget::SetDevice", "Media type set to %d", m_mediaType);
    cueParser = CUEParser(m_pDevice->GetCueSheet());
    data_skip_bytes = CDUtils::GetSkipbytes(this);
    data_block_size = CDUtils::GetBlocksize(this);

    if (bDiscSwap)
    {
        m_bPendingDiscSwap = true;
        m_nDiscSwapStartTick = CTimer::Get()->GetTicks();
        MLOGNOTE("CUSBCDGadget::SetDevice",
                 "Disc swap: Staying in NO_MEDIUM, will transition to UNIT_ATTENTION after delay");
    }
    else
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::SetDevice",
                        "Initial load: Deferring media ready state to OnActivate()");
    }

    u32 max_lba = CDUtils::GetLeadoutLBA(this);
    CUETrackInfo first_track = CDUtils::GetTrackInfoForLBA(this, 0);
    int first_track_blocksize = CDUtils::GetBlocksizeForTrack(this, first_track);
    CDROM_DEBUG_LOG("CUSBCDGadget::SetDevice",
                    "Disc info: max_lba=%u, track1_mode=%d, track1_blocksize=%d",
                    max_lba, first_track.track_mode, first_track_blocksize);
    CDROM_DEBUG_LOG("CUSBCDGadget::SetDevice",
                    "=== EXIT === m_CDReady=%d, mediaState=%d, sense=%02x/%02x/%02x",
                    m_CDReady, (int)m_mediaState,
                    m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);
}

void CUSBCDGadget::CreateDevice(void)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "entered");

    if (!m_pDevice)
    {
        MLOGDEBUG("CreateDevice called but m_pDevice is null - disc not ready");
        return; // Just return early, don't crash
    }
}

void CUSBCDGadget::OnSuspend(void)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::OnSuspend", "entered");
    delete m_pEP[EPOut];
    m_pEP[EPOut] = nullptr;

    delete m_pEP[EPIn];
    m_pEP[EPIn] = nullptr;

    m_nState = TCDState::Init;
}

const void *CUSBCDGadget::ToStringDescriptor(const char *pString, size_t *pLength)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::ToStringDescriptor", "entered");
    assert(pString);

    size_t nLength = 2;
    for (u8 *p = m_StringDescriptorBuffer + 2; *pString; pString++)
    {
        assert(nLength < sizeof m_StringDescriptorBuffer - 1);

        *p++ = (u8)*pString; // convert to UTF-16
        *p++ = '\0';

        nLength += 2;
    }

    m_StringDescriptorBuffer[0] = (u8)nLength;
    m_StringDescriptorBuffer[1] = DESCRIPTOR_STRING;

    assert(pLength);
    *pLength = nLength;

    return m_StringDescriptorBuffer;
}

int CUSBCDGadget::OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::OnClassOrVendorRequest", "entered");
    if (pSetupData->bmRequestType == 0xA1 && pSetupData->bRequest == 0xfe) // get max LUN
    {
        MLOGDEBUG("OnClassOrVendorRequest", "state = %i", m_nState);
        pData[0] = 0;
        return 1;
    }
    return -1;
}

void CUSBCDGadget::OnTransferComplete(boolean bIn, size_t nLength)
{
    // CDROM_DEBUG_LOG("OnXferComplete", "state = %i, dir = %s, len=%i ",m_nState,bIn?"IN":"OUT",nLength);
    assert(m_nState != TCDState::Init);
    if (bIn) // packet to host has been transferred
    {
        switch (m_nState)
        {
        case TCDState::SentCSW:
        {
            m_nState = TCDState::ReceiveCBW;
            m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut,
                                        m_OutBuffer, SIZE_CBW);
            break;
        }
        case TCDState::DataIn:
        {
            if (m_nnumber_blocks > 0)
            {
                if (m_CDReady)
                {
                    m_nState = TCDState::DataInRead; // see Update function
                }
                else
                {
                    MLOGERR("onXferCmplt DataIn", "failed, %s",
                            m_CDReady ? "ready" : "not ready");
                    m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                    m_SenseParams.bSenseKey = 0x02;
                    m_SenseParams.bAddlSenseCode = 0x04;     // LOGICAL UNIT NOT READY
                    m_SenseParams.bAddlSenseCodeQual = 0x00; // CAUSE NOT REPORTABLE
                    SendCSW();
                }
            }
            else // done sending data to host
            {
                SendCSW();
            }
            break;
        }
        case TCDState::SendReqSenseReply:
        {
            SendCSW();
            break;
        }
        default:
        {
            MLOGERR("onXferCmplt", "dir=in, unhandled state = %i", m_nState);
            assert(0);
            break;
        }
        }
    }
    else // packet from host is available in m_OutBuffer
    {
        switch (m_nState)
        {
        case TCDState::ReceiveCBW:
        {
            if (nLength != SIZE_CBW)
            {
                MLOGERR("ReceiveCBW", "Invalid CBW len = %i", nLength);
                m_pEP[EPIn]->StallRequest(true);
                break;
            }
            memcpy(&m_CBW, m_OutBuffer, SIZE_CBW);
            // MLOGNOTE("ReceiveCBW", "*** CBW RECEIVED *** cmd=0x%02x, mediaState=%d, m_CDReady=%d",
            //  m_CBW.CBWCB[0], (int)m_mediaState, m_CDReady);
            if (m_CBW.dCBWSignature != VALID_CBW_SIG)
            {
                MLOGERR("ReceiveCBW", "Invalid CBW sig = 0x%x",
                        m_CBW.dCBWSignature);
                m_pEP[EPIn]->StallRequest(true);
                break;
            }
            m_CSW.dCSWTag = m_CBW.dCBWTag;
            if (m_CBW.bCBWCBLength <= 16 && m_CBW.bCBWLUN == 0) // meaningful CBW
            {
                HandleSCSICommand(); // will update m_nstate
                break;
            } // TODO: response for not meaningful CBW
            break;
        }

        case TCDState::DataOut:
        {
            CDROM_DEBUG_LOG("OnXferComplete", "state = %i, dir = %s, len=%i ", m_nState, bIn ? "IN" : "OUT", nLength);
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

        default:
        {
            MLOGERR("onXferCmplt", "dir=out, unhandled state = %i", m_nState);
            assert(0);
            break;
        }
        }
    }
}

void CUSBCDGadget::ProcessOut(size_t nLength)
{
    // This code is assuming that the payload is a Mode Select payload.
    // At the moment, this is the only thing likely to appear here.
    // TODO: somehow validate what this data is

    CDROM_DEBUG_LOG("ProcessOut",
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

    switch (modePage)
    {
    // CDROM Audio Control Page
    case 0x0e:
    {
        ModePage0x0EData *modePage = (ModePage0x0EData *)(m_OutBuffer + 8);
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Select (10), Volume is %u,%u", modePage->Output0Volume, modePage->Output1Volume);
        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {

            // Descent 2 sets the volume weird. For each volume change, it sends
            // the following in quick succession :-
            // Mode Select (10), Volume is 0,255
            // Mode Select (10), Volume is 255,0
            // Mode Select (10), Volume is 74,255
            // Mode Select (10), Volume is 255,74
            // So, we'll pick the minimum of the two

            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "CDPlayer set volume");
            cdplayer->SetVolume(
                modePage->Output0Volume < modePage->Output1Volume
                    ? modePage->Output0Volume
                    : modePage->Output1Volume);
        }
        else
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Couldn't get CDPlayer");
        }
        break;
    }
    }
}

// will be called before vendor request 0xfe
void CUSBCDGadget::OnActivate()
{
    CDROM_DEBUG_LOG("CD OnActivate",
                    "=== ENTRY === state=%d, USB=%s, m_CDReady=%d, mediaState=%d",
                    (int)m_nState,
                    m_IsFullSpeed ? "Full-Speed (USB 1.1)" : "High-Speed (USB 2.0)",
                    m_CDReady, (int)m_mediaState);
    // Determine negotiated USB speed
    TDeviceSpeed negotiatedSpeed = GetSpeed(); // This calls CDWUSBGadget::GetSpeed()
    m_IsFullSpeed = (negotiatedSpeed == FullSpeed);

    CDROM_DEBUG_LOG("CD OnActivate",
                    "Negotiated speed: %s (m_IsFullSpeed=%d)",
                    m_IsFullSpeed ? "Full-Speed" : "High-Speed",
                    m_IsFullSpeed);

    // Set media ready NOW - USB endpoints are active
    if (m_pDevice && !m_CDReady)
    {
        m_CDReady = true;
        m_mediaState = MediaState::MEDIUM_PRESENT_UNIT_ATTENTION;
        m_SenseParams.bSenseKey = 0x06;
        m_SenseParams.bAddlSenseCode = 0x28;
        m_SenseParams.bAddlSenseCodeQual = 0x00;
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        discChanged = true;
        CDROM_DEBUG_LOG("CD OnActivate",
                        "Initial media ready: Set UNIT_ATTENTION, sense=06/28/00");
    }

    m_nState = TCDState::ReceiveCBW;
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);

    CDROM_DEBUG_LOG("CD OnActivate",
                    "=== EXIT === Waiting for CBW, m_CDReady=%d, mediaState=%d",
                    m_CDReady, (int)m_mediaState);
}

void CUSBCDGadget::SendCSW()
{
    // CDROM_DEBUG_LOG ("CUSBCDGadget::SendCSW", "entered");
    memcpy(&m_InBuffer, &m_CSW, SIZE_CSW);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCSWIn, m_InBuffer, SIZE_CSW);
    m_nState = TCDState::SentCSW;
}

// Sense data management helpers for MacOS compatibility
// Based on BlueSCSI patterns but adapted for USBODE architecture
void CUSBCDGadget::setSenseData(u8 senseKey, u8 asc, u8 ascq)
{
    m_SenseParams.bSenseKey = senseKey;
    m_SenseParams.bAddlSenseCode = asc;
    m_SenseParams.bAddlSenseCodeQual = ascq;

    MLOGDEBUG("setSenseData", "Sense: %02x/%02x/%02x", senseKey, asc, ascq);
}

void CUSBCDGadget::clearSenseData()
{
    m_SenseParams.bSenseKey = 0x00;
    m_SenseParams.bAddlSenseCode = 0x00;
    m_SenseParams.bAddlSenseCodeQual = 0x00;
}

void CUSBCDGadget::sendCheckCondition()
{
    m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
    // USB Mass Storage spec: data residue = amount of expected data not transferred
    // For CHECK CONDITION with no data phase, residue = full requested length
    m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength;
    SendCSW();
}

void CUSBCDGadget::sendGoodStatus()
{
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    m_CSW.dCSWDataResidue = 0; // Command succeeded, all data (if any) transferred
    SendCSW();
}

void CUSBCDGadget::HandleSCSICommand()
{
    if (m_CBW.CBWCB[0] != 0x00) // Filter out TEST_UNIT_READY spam
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "SCSI Command is 0x%02x", m_CBW.CBWCB[0]);
    }
    // Centralized Unit Attention Check
    // Some commands (like INQUIRY) must work even if Unit Attention is pending.
    // Others (like READ) must fail so the host knows the media changed.
    if (m_mediaState == MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        u8 cmd = m_CBW.CBWCB[0];
        bool blocked = false;

        // Block commands that actually READ or PLAY disc data
        if (cmd == 0x28)
            blocked = true; // READ 10
        else if (cmd == 0xA8)
            blocked = true; // READ 12
        else if (cmd == 0xBE)
            blocked = true; // READ CD
        else if (cmd == 0x45)
            blocked = true; // PLAY AUDIO 10
        else if (cmd == 0xA5)
            blocked = true; // PLAY AUDIO 12
        else if (cmd == 0x47)
            blocked = true; // PLAY AUDIO MSF
        else if (cmd == 0x2B)
            blocked = true; // SEEK

        if (blocked)
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "Command 0x%02x -> CHECK CONDITION (sense 06/28/00 - UNIT ATTENTION)", cmd);
            setSenseData(0x06, 0x28, 0x00); // UNIT ATTENTION - MEDIA CHANGED
            sendCheckCondition();
            CTimer::Get()->MsDelay(10);
            return;
        }
    }

    // Lookup command handler
    u8 cmdCode = m_CBW.CBWCB[0];
    SCSIHandler handler = m_SCSIHandlers[cmdCode];

    if (handler != nullptr)
    {
        handler(this);
    }
    else
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Unknown SCSI Command is 0x%02x", cmdCode);
        setSenseData(0x05, 0x20, 0x00); // INVALID COMMAND OPERATION CODE
        sendCheckCondition();
    }
}
