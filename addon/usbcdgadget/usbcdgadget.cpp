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
#include <configservice/configservice.h>

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

const TUSBDeviceDescriptor CUSBCDGadget::s_DeviceDescriptor =
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
            0x80,   // bmAttributes (bus-powered)
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

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorHighSpeed =
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

CUSBCDGadget::CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, ICueDevice *pDevice)
    : CDWUSBGadget(pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr}
{
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "entered %d", isFullSpeed);
    m_IsFullSpeed = isFullSpeed;

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

    // Read debug logging flag from config.txt
    ConfigService *configService = (ConfigService *)CScheduler::Get()->GetTask("configservice");
    if (configService)
    {
        m_bDebugLogging = configService->GetProperty("debug_cdrom", 0U) != 0;
        if (m_bDebugLogging)
        {
            MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "CD-ROM debug logging enabled");
        }
    }
    else
    {
        m_bDebugLogging = false; // Default to disabled if config service not available
    }

    if (pDevice)
        SetDevice(pDevice);
}

CUSBCDGadget::~CUSBCDGadget(void)
{
    assert(0);
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
            *pLength = sizeof s_DeviceDescriptor;
            return &s_DeviceDescriptor;
        }
        break;

    case DESCRIPTOR_CONFIGURATION:
        CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_CONFIGURATION %02x", uchDescIndex);
        if (!uchDescIndex)
        {
            *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
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
            const char *desc_name = "";
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
    if (m_IsFullSpeed)
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor *>(
                &s_ConfigurationDescriptorFullSpeed.EndpointOut),
            this);
    else
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor *>(
                &s_ConfigurationDescriptorHighSpeed.EndpointOut),
            this);
    assert(m_pEP[EPOut]);

    assert(!m_pEP[EPIn]);
    if (m_IsFullSpeed)
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor *>(
                &s_ConfigurationDescriptorFullSpeed.EndpointIn),
            this);
    else
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor *>(
                &s_ConfigurationDescriptorHighSpeed.EndpointIn),
            this);
    assert(m_pEP[EPIn]);

    m_nState = TCDState::Init;
}

// must set device before usb activation
void CUSBCDGadget::SetDevice(ICueDevice *dev)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::SetDevice", "entered");

    // Hand the new device to the CD Player
    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "Passed CueBinFileDevice to cd player");
    }

    // Are we changing the device?
    if (m_pDevice && m_pDevice != dev)
    {
        MLOGNOTE("CUSBCDGadget::SetDevice", "Changing device - ejecting old media");

        // We own this pointer now, so free the memory
        delete m_pDevice;
        m_pDevice = nullptr;

        // Tell the host the disc has changed
        // TODO: implement a state engine to manage this transition
        m_CDReady = false;
        m_mediaState = MediaState::NO_MEDIUM;
        m_SenseParams.bSenseKey = 0x02;      // Not Ready
        m_SenseParams.bAddlSenseCode = 0x3a; // MEDIUM NOT PRESENT
        m_SenseParams.bAddlSenseCodeQual = 0x00;
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        discChanged = true;
    }

    m_pDevice = dev;
    m_mediaType = m_pDevice->GetMediaType();
    MLOGNOTE("CUSBCDGadget::SetDevice", "Media type set to %d", m_mediaType);
    cueParser = CUEParser(m_pDevice->GetCueSheet()); // FIXME. Ensure cuesheet is not null or empty

    MLOGNOTE("CUSBCDGadget::SetDevice", "entered");

    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();

    m_CDReady = true;
    m_mediaState = MediaState::MEDIUM_PRESENT_UNIT_ATTENTION;
    m_SenseParams.bSenseKey = 0x06;
    m_SenseParams.bAddlSenseCode = 0x28; // MEDIUM MAY HAVE CHANGED
    m_SenseParams.bAddlSenseCodeQual = 0x00;
    bmCSWStatus = CD_CSW_STATUS_FAIL;
    discChanged = true;
    CDROM_DEBUG_LOG("CUSBCDGadget::SetDevice", "Block size is %d, m_CDReady = %d", block_size, m_CDReady);
}

int CUSBCDGadget::GetBlocksize()
{
    cueParser.restart();
    const CUETrackInfo *trackInfo = cueParser.next_track();
    return GetBlocksizeForTrack(*trackInfo);
}

int CUSBCDGadget::GetBlocksizeForTrack(CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
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
        MLOGERR("CUSBCDGadget::GetBlocksizeForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

int CUSBCDGadget::GetSkipbytes()
{
    cueParser.restart();
    const CUETrackInfo *trackInfo = cueParser.next_track();
    return GetSkipbytesForTrack(*trackInfo);
}

int CUSBCDGadget::GetSkipbytesForTrack(CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
        MLOGDEBUG("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE1_2048");
        return 0;
    case CUETrack_MODE1_2352:
        MLOGDEBUG("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE1_2352");
        return 16;
    case CUETrack_MODE2_2352:
        MLOGDEBUG("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_MODE2_2352");
        return 24;
    case CUETrack_AUDIO:
        MLOGDEBUG("CUSBCDGadget::GetSkipbytesForTrack", "CUETrack_AUDIO");
        return 0;
    default:
        MLOGERR("CUSBCDGadget::GetSkipbytesForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

// Make an assumption about media type based on track 1 mode
int CUSBCDGadget::GetMediumType()
{
    cueParser.restart();
    const CUETrackInfo *trackInfo = nullptr;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr)
    {
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

CUETrackInfo CUSBCDGadget::GetTrackInfoForTrack(int track)
{
    const CUETrackInfo *trackInfo = nullptr;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number == track)
        {
            return *trackInfo; // Safe copy — all fields are POD
        }
    }

    CUETrackInfo invalid = {};
    invalid.track_number = -1;
    return invalid;
}

CUETrackInfo CUSBCDGadget::GetTrackInfoForLBA(u32 lba)
{
    const CUETrackInfo *trackInfo;
    MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Searching for LBA %u", lba);

    cueParser.restart();

    // Shortcut for LBA zero
    if (lba == 0)
    {
        MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Shortcut lba == 0 returning first track");
        const CUETrackInfo *firstTrack = cueParser.next_track(); // Return the first track
        if (firstTrack != nullptr)
        {
            return *firstTrack;
        }
        else
        {
            CUETrackInfo invalid = {};
            invalid.track_number = -1;
            return invalid;
        }
    }

    // Iterate to find our track
    CUETrackInfo lastTrack = {};
    lastTrack.track_number = -1;
    while ((trackInfo = cueParser.next_track()) != nullptr)
    {
        MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Iterating: Current Track %d track_start is %lu", trackInfo->track_number, trackInfo->track_start);

        //  Shortcut for when our LBA is the start address of this track
        if (trackInfo->track_start == lba)
        {
            MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Shortcut track_start == lba, returning track %d", trackInfo->track_number);
            return *trackInfo;
        }

        if (lba < trackInfo->track_start)
        {
            MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Found LBA %lu in track %d", lba, lastTrack.track_number);
            return lastTrack;
        }

        lastTrack = *trackInfo;
    }

    MLOGDEBUG("CUSBCDGadget::GetTrackInfoForLBA", "Returning last track");
    return lastTrack;
}

u32 CUSBCDGadget::GetLeadoutLBA()
{
    const CUETrackInfo *trackInfo = nullptr;
    u32 file_offset = 0;
    u32 sector_length = 0;
    u32 track_start = 0;

    // Find the last track
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr)
    {
        file_offset = trackInfo->file_offset;
        sector_length = trackInfo->sector_length;
        track_start = trackInfo->data_start; // I think this is right
    }

    u64 deviceSize = m_pDevice->GetSize(); // Use u64 to support DVDs > 4GB

    // We know the start position of the last track, and we know its sector length
    // and we know the file size, so we can work out the LBA of the end of the last track
    // We can't just divide the file size by sector size because sectors lengths might
    // not be consistent (e.g. multi-mode cd where track 1 is 2048
    u64 lastTrackBlocks = (deviceSize - file_offset) / sector_length;
    u32 ret = track_start + (u32)lastTrackBlocks; // Cast back to u32 for LBA (max ~2TB disc)
    CDROM_DEBUG_LOG("CUSBCDGadget::GetLeadoutLBA", "device size is %llu, last track file offset is %lu, last track sector_length is %lu, last track track_start is %lu, lastTrackBlocks = %llu, returning = %lu", deviceSize, file_offset, sector_length, track_start, lastTrackBlocks, ret);

    // Some corrupted cd images might have a cue that references track that are
    // outside the bin.
    if (deviceSize < file_offset)
        return track_start;

    return ret;
}

int CUSBCDGadget::GetLastTrackNumber()
{
    const CUETrackInfo *trackInfo = nullptr;
    int lastTrack = 1;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number > lastTrack)
            lastTrack = trackInfo->track_number;
    }
    return lastTrack;
}

void CUSBCDGadget::CreateDevice(void)
{
    CDROM_DEBUG_LOG("CUSBCDGadget::GetDescriptor", "entered");
    assert(m_pDevice);
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
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Select (10), Volume is %u,%u", modePage->Output0Volume, modePage->Output1Volume);
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

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CDPlayer set volume");
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
    MLOGNOTE("CD OnActivate", "state = %i", m_nState);
    m_CDReady = true;
    m_nState = TCDState::ReceiveCBW;
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);
}

void CUSBCDGadget::SendCSW()
{
    // CDROM_DEBUG_LOG ("CUSBCDGadget::SendCSW", "entered");
    memcpy(&m_InBuffer, &m_CSW, SIZE_CSW);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCSWIn, m_InBuffer, SIZE_CSW);
    m_nState = TCDState::SentCSW;
}

u32 CUSBCDGadget::msf_to_lba(u8 minutes, u8 seconds, u8 frames)
{
    // Combine minutes, seconds, and frames into a single LBA-like value
    // The u8 inputs will be promoted to int/u32 for the arithmetic operations
    u32 lba = ((u32)minutes * 60 * 75) + ((u32)seconds * 75) + (u32)frames;

    // Adjust for the 150-frame (2-second) offset.
    lba = lba - 150;

    return lba;
}

u32 CUSBCDGadget::lba_to_msf(u32 lba, boolean relative)
{
    if (!relative)
        lba = lba + 150; // MSF values are offset by 2mins. Weird

    u8 minutes = lba / (75 * 60);
    u8 seconds = (lba / 75) % 60;
    u8 frames = lba % 75;
    u8 reserved = 0;

    return (frames << 24) | (seconds << 16) | (minutes << 8) | reserved;
}

u32 CUSBCDGadget::GetAddress(u32 lba, int msf, boolean relative)
{
    u32 address = lba;
    if (msf)
        return lba_to_msf(lba, relative);
    return htonl(address);
}

int CUSBCDGadget::GetSectorLengthFromMCS(uint8_t mainChannelSelection)
{
    int total = 0;
    if (mainChannelSelection & 0x10)
        total += 12; // SYNC
    if (mainChannelSelection & 0x08)
        total += 4; // HEADER
    if (mainChannelSelection & 0x04)
        total += 2048; // USER DATA
    if (mainChannelSelection & 0x02)
        total += 288; // EDC + ECC

    return total;
}

int CUSBCDGadget::GetSkipBytesFromMCS(uint8_t mainChannelSelection)
{
    int offset = 0;

    // Skip SYNC if not requested
    if (!(mainChannelSelection & 0x10))
        offset += 12;

    // Skip HEADER if not requested
    if (!(mainChannelSelection & 0x08))
        offset += 4;

    // USER DATA is next; if also not requested, skip 2048
    if (!(mainChannelSelection & 0x04))
        offset += 2048;

    // EDC/ECC is always at the end, so no skipping here — it doesn't affect offset
    //
    return offset;
}

// Sense data management helpers for MacOS compatibility
// Based on BlueSCSI patterns but adapted for USBODE architecture
void CUSBCDGadget::setSenseData(u8 senseKey, u8 asc, u8 ascq) {
    m_SenseParams.bSenseKey = senseKey;
    m_SenseParams.bAddlSenseCode = asc;
    m_SenseParams.bAddlSenseCodeQual = ascq;
    
    MLOGDEBUG("setSenseData", "Sense: %02x/%02x/%02x", senseKey, asc, ascq);
}

void CUSBCDGadget::clearSenseData() {
    m_SenseParams.bSenseKey = 0x00;
    m_SenseParams.bAddlSenseCode = 0x00;
    m_SenseParams.bAddlSenseCodeQual = 0x00;
}

void CUSBCDGadget::sendCheckCondition() {
    m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
    // USB Mass Storage spec: data residue = amount of expected data not transferred
    // For CHECK CONDITION with no data phase, residue = full requested length
    m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength;
    SendCSW();
}

void CUSBCDGadget::sendGoodStatus() {
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    m_CSW.dCSWDataResidue = 0;  // Command succeeded, all data (if any) transferred
    SendCSW();
}

void FillModePage2A(ModePage0x2AData &codepage)
{
    memset(&codepage, 0, sizeof(codepage));
    codepage.pageCodeAndPS = 0x2a;
    codepage.pageLength = 22; // Should be 22 bytes for full MMC-5 compliance

    // Capability bits (6 bytes) - dynamic based on media type
    // Byte 0: bit0=DVD-ROM, bit1=DVD-R, bit2=DVD-RAM, bit3=CD-R, bit4=CD-RW, bit5=Method2
    codepage.capabilityBits[0] = 0x01; // Support all media types for DVD, else CD only
    codepage.capabilityBits[1] = 0x00; // All writable types
    codepage.capabilityBits[2] = 0x01; // AudioPlay, composite audio/video, digital port 2, Mode 2 Form 2, Mode 2 Form 1
    codepage.capabilityBits[3] = 0x03; // CD-DA Commands Supported, CD-DA Stream is accurate
    codepage.capabilityBits[4] = 0x28; // Tray loading mechanism, eject supported, lock supported
    codepage.capabilityBits[5] = 0x00; // No separate channel volume, no separate channel mute

    // Speed and buffer info
    codepage.maxSpeed = htons(1412);          // 8x
    codepage.numVolumeLevels = htons(0x00ff); // 256 volume levels
    codepage.bufferSize = htons(0);           // Set to 0
    codepage.currentSpeed = htons(1412);      // Current speed
    codepage.maxReadSpeed = htons(1412);      // Some hosts check this field
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
void CUSBCDGadget::HandleSCSICommand()
{
    // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "SCSI Command is 0x%02x", m_CBW.CBWCB[0]);
    switch (m_CBW.CBWCB[0])
    {
    case 0x00: // Test unit ready
    {
        if (!m_CDReady)
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
            bmCSWStatus = CD_CSW_STATUS_FAIL;
            m_SenseParams.bSenseKey = 2;
            m_SenseParams.bAddlSenseCode = 0x04;     // LOGICAL UNIT NOT READY
            m_SenseParams.bAddlSenseCodeQual = 0x00; // CAUSE NOT REPORTABLE
        }

        // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x03: // Request sense CMD
    {
        // This command is the host asking why the last command generated a check condition
        // We'll clear the reason after we've communicated it. If it's still an issue, we'll
        // throw another Check Condition afterwards
        // bool desc = m_CBW.CBWCB[1] & 0x01;
        u8 blocks = (u8)(m_CBW.CBWCB[4]);

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Request Sense CMD: bSenseKey 0x%02x, bAddlSenseCode 0x%02x, bAddlSenseCodeQual 0x%02x ", m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);

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

        // If we were "Not Ready", switch to Unit Attention
        if (m_SenseParams.bSenseKey == 0x02)
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Moving sense state to Unit Attention, Medium have have changed");
            bmCSWStatus = CD_CSW_STATUS_FAIL;
            m_SenseParams.bSenseKey = 0x06;          // Unit Attention
            m_SenseParams.bAddlSenseCode = 0x28;     // NOT READY TO READY CHANGE
            m_SenseParams.bAddlSenseCodeQual = 0x00; // MEDIUM MAY HAVE CHANGED
        }
        else
        {
            // Reset response params after send
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Moving sense state to OK");
            bmCSWStatus = CD_CSW_STATUS_OK;
            m_SenseParams.bSenseKey = 0;          // NO SENSE
            m_SenseParams.bAddlSenseCode = 0;     // NO ADDITIONAL SENSE INFORMATION
            m_SenseParams.bAddlSenseCodeQual = 0; // NO ADDITIONAL SENSE INFORMATION
        }
        break;
    }

    case 0xa8: // Read (12) - similar to READ(10) but with 32-bit block count
    {
        if (m_CDReady)
        {
            // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "Read (12)");
            // will be updated if read fails on any block
            m_CSW.bmCSWStatus = bmCSWStatus;

            // Where to start reading (LBA) - 4 bytes
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) |
                               (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

            // Number of blocks to read (LBA) - 4 bytes
            m_nnumber_blocks = (u32)(m_CBW.CBWCB[6] << 24) | (u32)(m_CBW.CBWCB[7] << 16) |
                               (u32)(m_CBW.CBWCB[8] << 8) | m_CBW.CBWCB[9];

            // Transfer Block Size is the size of data to return to host
            // Block Size and Skip Bytes is worked out from cue sheet
            // For a CDROM, this is always 2048
            transfer_block_size = 2048;
            block_size = data_block_size; // set at SetDevice
            skip_bytes = data_skip_bytes; // set at SetDevice;
            mcs = 0;

            m_nbyteCount = m_CBW.dCBWDataTransferLength;

            // What is this?
            if (m_nnumber_blocks == 0)
            {
                m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;
            }
            m_CSW.bmCSWStatus = bmCSWStatus;
            m_nState = TCDState::DataInRead; // see Update() function
        }
        else
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ(12) failed, %s", m_CDReady ? "ready" : "not ready");
            m_SenseParams.bSenseKey = 0x02;          // Not Ready
            m_SenseParams.bAddlSenseCode = 0x04;     // LOGICAL UNIT NOT READY
            m_SenseParams.bAddlSenseCodeQual = 0x00; // MEDIUM MAY HAVE CHANGED
            m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        }
        break;
    }

    case 0x12: // Inquiry
    {
        int allocationLength = (m_CBW.CBWCB[3] << 8) | m_CBW.CBWCB[4];
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Inquiry %0x, allocation length %d", m_CBW.CBWCB[1], allocationLength);

        if ((m_CBW.CBWCB[1] & 0x01) == 0)
        { // EVPD bit is 0: Standard Inquiry
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Inquiry (Standard Enquiry)");

            // Set response length
            int datalen = SIZE_INQR;
            if (allocationLength < datalen)
                datalen = allocationLength;

            memcpy(&m_InBuffer, &m_InqReply, datalen);
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, datalen);
            m_nState = TCDState::DataIn;
            m_nnumber_blocks = 0; // nothing more after this send
            // m_CSW.bmCSWStatus = bmCSWStatus;
            m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        }
        else
        { // EVPD bit is 1: VPD Inquiry
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Inquiry (VPD Inquiry)");
            u8 vpdPageCode = m_CBW.CBWCB[2];
            switch (vpdPageCode)
            {
            case 0x00: // Supported VPD Pages
            {
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Inquiry (Supported VPD Pages)");

                u8 SupportedVPDPageReply[] = {
                    0x05, // Byte 0: Peripheral Device Type (0x05 for Optical Memory Device)
                    0x00, // Byte 1: Page Code (0x00 for Supported VPD Pages page)
                    0x00, // Byte 2: Page Length (MSB) - total length of page codes following
                    0x03, // Byte 3: Page Length (LSB) - 3 supported page codes
                    0x00, // Byte 4: Supported VPD Page Code: Supported VPD Pages (this page itself)
                    0x80, // Byte 5: Supported VPD Page Code: Unit Serial Number
                    0x83  // Byte 6: Supported VPD Page Code: Device Identification
                };

                // Set response length
                int datalen = sizeof(SupportedVPDPageReply);
                if (allocationLength < datalen)
                    datalen = allocationLength;

                memcpy(&m_InBuffer, &SupportedVPDPageReply, sizeof(SupportedVPDPageReply));
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                           m_InBuffer, datalen);
                m_nState = TCDState::DataIn;
                m_nnumber_blocks = 0; // nothing more after this send
                // m_CSW.bmCSWStatus = bmCSWStatus;
                m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                break;
            }

            case 0x80: // Unit Serial Number Page
            {
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unit Serial number Page)");

                u8 UnitSerialNumberReply[] = {
                    0x05, // Byte 0: Peripheral Device Type (Optical Memory Device)
                    0x80, // Byte 1: Page Code (Unit Serial Number page)
                    0x00, // Byte 2: Page Length (MSB) - Length of serial number data
                    0x0B, // Byte 3: Page Length (LSB) - 11 bytes follow
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
                m_nnumber_blocks = 0; // nothing more after this send
                // m_CSW.bmCSWStatus = bmCSWStatus;
                m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                break;
            }

            case 0x83:
            {
                u8 DeviceIdentificationReply[] = {
                    0x05, // Byte 0: Peripheral Device Type (Optical Memory Device)
                    0x83, // Byte 1: Page Code (Device Identification page)
                    0x00, // Byte 2: Page Length (MSB)
                    0x0B, // Byte 3: Page Length (LSB) - Total length of all designators combined (11 bytes in this example)

                    // --- Start of First Designator (T10 Vendor ID) ---
                    0x01, // Byte 4: CODE SET (0x01 = ASCII)
                          //         PIV (0) + Assoc (0) + Type (0x01 = T10 Vendor ID)
                    0x00, // Byte 5: PROTOCOL IDENTIFIER (0x00 = SCSI)
                    0x08, // Byte 6: LENGTH (Length of the identifier data itself - 8 bytes)
                    // Bytes 7-14: IDENTIFIER (Your T10 Vendor ID, padded to 8 bytes)
                    'U', 'S', 'B', 'O', 'D', 'E', ' ', ' ' // "USBODE  " - padded to 8 bytes
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
                m_nnumber_blocks = 0; // nothing more after this send
                // m_CSW.bmCSWStatus = bmCSWStatus;
                m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                break;
            }

            default: // Unsupported VPD Page
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unsupported Page)");
                //  m_nState = TCDState::DataIn;
                m_nnumber_blocks = 0; // nothing more after this send

                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
                m_SenseParams.bSenseKey = 0x05;
                m_SenseParams.bAddlSenseCode = 0x24;     // Invalid Field
                m_SenseParams.bAddlSenseCodeQual = 0x00; // In CDB
                SendCSW();
                break;
            }
        }
        break;
    }

    case 0x1B: // Start/stop unit
    {
        int start = m_CBW.CBWCB[4] & 1;
        int loej = (m_CBW.CBWCB[4] >> 1) & 1;
        // TODO: Emulate a disk eject/load
        // loej Start Action
        // 0    0     Stop the disc - no action for us
        // 0    1     Start the disc - no action for us
        // 1    0     Eject the disc - perhaps we need to throw a check condition?
        // 1    1     Load the disc - perhaps we need to throw a check condition?

        CDROM_DEBUG_LOG("HandleSCSI", "start/stop, start = %d, loej = %d", start, loej);
        // m_CSW.bmCSWStatus = bmCSWStatus;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        SendCSW();
        break;
    }

    case 0x1E: // PREVENT ALLOW MEDIUM REMOVAL
    {
        // Lie to the host
        // m_CSW.bmCSWStatus = bmCSWStatus;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        SendCSW();
        break;
    }

    case 0x25: // Read Capacity (10))
    {
        m_ReadCapReply.nLastBlockAddr = htonl(GetLeadoutLBA() - 1); // this value is the Start address of last recorded lead-out minus 1
        memcpy(&m_InBuffer, &m_ReadCapReply, SIZE_READCAPREP);
        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, SIZE_READCAPREP);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    case 0x28: // Read (10)
    {
        if (m_CDReady)
        {
            // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "Read (10)");
            // will be updated if read fails on any block
            m_CSW.bmCSWStatus = bmCSWStatus;

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

            // Number of blocks to read (LBA)
            m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);

            // Transfer Block Size is the size of data to return to host
            // Block Size and Skip Bytes is worked out from cue sheet
            // For a CDROM, this is always 2048
            transfer_block_size = 2048;
            block_size = data_block_size; // set at SetDevice
            skip_bytes = data_skip_bytes; // set at SetDevice;
            mcs = 0;

            m_nbyteCount = m_CBW.dCBWDataTransferLength;

            // What is this?
            if (m_nnumber_blocks == 0)
            {
                m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;
            }
            m_CSW.bmCSWStatus = bmCSWStatus;
            m_nState = TCDState::DataInRead; // see Update() function
        }
        else
        {
            CDROM_DEBUG_LOG("handleSCSI Read(10)", "failed, %s", m_CDReady ? "ready" : "not ready");
            m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
            m_SenseParams.bSenseKey = 0x02;
            m_SenseParams.bAddlSenseCode = 0x04;     // LOGICAL UNIT NOT READY
            m_SenseParams.bAddlSenseCodeQual = 0x00; // CAUSE NOT REPORTABLE
            SendCSW();
        }
        break;
    }

    case 0xBE: // READ CD
    {
        if (m_CDReady)
        {

            // Expected Sector Type. We can use this to derive
            // sector size and offset in most cases
            int expectedSectorType = (m_CBW.CBWCB[1] >> 2) & 0x07;

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
            // Number of blocks to read (LBA)
            m_nnumber_blocks = (u32)(m_CBW.CBWCB[6] << 16) | (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);

            mcs = (m_CBW.CBWCB[9] >> 3) & 0x1F;

            // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "READ CD for %lu blocks at LBA %lu of type %02x", m_nnumber_blocks, m_nblock_address, expectedSectorType);
            switch (expectedSectorType)
            {
            case 0x01:
            {
                // CD-DA
                block_size = 2352;
                transfer_block_size = 2352;
                skip_bytes = 0;
                break;
            }
            case 0x02:
            {
                // Mode 1
                CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
                skip_bytes = GetSkipbytesForTrack(trackInfo);
                block_size = GetBlocksizeForTrack(trackInfo);
                transfer_block_size = 2048;
                break;
            }
            case 0x03:
            {
                // Mode 2 formless
                skip_bytes = 16;
                block_size = 2352;
                transfer_block_size = 2336;
                break;
            }
            case 0x04:
            {
                // Mode 2 form 1
                CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
                skip_bytes = GetSkipbytesForTrack(trackInfo);
                block_size = GetBlocksizeForTrack(trackInfo);
                transfer_block_size = 2048;
                break;
            }
            case 0x05:
            {
                // Mode 2 form 2
                block_size = 2352;
                skip_bytes = 24;
                transfer_block_size = 2048;
                break;
            }
            case 0x00:
            default:
            {
                // Client doesn't tell us what data type he's expecting. He expects us
                // to work it out based on the MCS flags
                CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);

                // Audio tracks have no concept of MCS, so we just return all 2352 bytes
                if (trackInfo.track_mode == CUETrack_AUDIO)
                {
                    block_size = 2352;
                    transfer_block_size = 2352;
                    skip_bytes = 0;
                }
                else
                {
                    // This gives us a horrible situation where we might be using a
                    // underlying image file with block sizes of 2048 but host requests
                    // block size of 2352, so the Update function at the end needs to
                    // synthesize some bytes to make up for the difference
                    block_size = GetBlocksizeForTrack(trackInfo);
                    transfer_block_size = GetSectorLengthFromMCS(mcs);
                    skip_bytes = GetSkipBytesFromMCS(mcs);
                }
                break;
            }
            }

            MLOGDEBUG("CUSBCDGadget::HandleSCSICommand", "READ CD for %lu blocks at LBA %lu of type %02x, block_size = %d, skip_bytes = %d, transfer_block_ssize = %d", m_nnumber_blocks, m_nblock_address, expectedSectorType, block_size, skip_bytes, transfer_block_size);

            // What is this?
            m_nbyteCount = m_CBW.dCBWDataTransferLength;
            if (m_nnumber_blocks == 0)
            {
                m_nnumber_blocks = 1 + (m_nbyteCount) / 2048; // fixme?
            }

            m_nState = TCDState::DataInRead; // see Update() function
            m_CSW.bmCSWStatus = bmCSWStatus;
        }
        else
        {
            MLOGNOTE("handleSCSI READ CD", "failed, %s", m_CDReady ? "ready" : "not ready");
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
        }
        break;
    }

    // These commands are not implemented so we lie about it
    case 0xBB: // Set CDROM Speed
    case 0x2F: // Verify
    {
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        SendCSW();
        break;
    }

    case 0x43: // READ TOC/PMA/ATIP
    {
        if (m_CDReady)
        {
            int msf = (m_CBW.CBWCB[1] >> 1) & 0x01;
            int format = m_CBW.CBWCB[2] & 0x07; // TODO implement formats. Currently we assume it's always 0x00
            int startingTrack = m_CBW.CBWCB[6];
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Read TOC with format = %d, msf = %02x, starting track = %d, allocation length = %d, m_CDReady = %d", format, msf, startingTrack, allocationLength, m_CDReady);

            TUSBTOCData m_TOCData;
            TUSBTOCEntry *tocEntries;

            int numtracks = 0;
            int datalen = 0;

            if (format == 0x00)
            { // Read TOC Data Format (With Format Field = 00b)

                const CUETrackInfo *trackInfo = nullptr;
                int lastTrackNumber = GetLastTrackNumber();

                // Header
                m_TOCData.FirstTrack = 0x01;
                m_TOCData.LastTrack = lastTrackNumber;
                datalen = SIZE_TOC_DATA;

                // Populate the track entries
                tocEntries = new TUSBTOCEntry[lastTrackNumber + 1];

                int index = 0;
                if (startingTrack != 0xAA)
                { // Do we only want the leadout?
                    cueParser.restart();
                    while ((trackInfo = cueParser.next_track()) != nullptr)
                    {
                        if (trackInfo->track_number < startingTrack)
                            continue;
                        boolean relative = false;
                        // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "Adding at index %d: track number = %d, track_start = %d, start lba or msf %d", index, trackInfo->track_number, trackInfo->track_start, GetAddress(trackInfo->track_start, msf));
                        tocEntries[index].ADR_Control = 0x14;
                        if (trackInfo->track_mode == CUETrack_AUDIO)
                        {
                            tocEntries[index].ADR_Control = 0x10;
                        }
                        tocEntries[index].reserved = 0x00;
                        tocEntries[index].TrackNumber = trackInfo->track_number;
                        tocEntries[index].reserved2 = 0x00;
                        tocEntries[index].address = GetAddress(trackInfo->track_start, msf);
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

                //} else if (format == 0x01) { // Read TOC Data Format (With Format Field = 01b)
            }
            else if (format == 0x02)
            { // Full TOC
                // Full TOC includes more detailed track information
                // Minimum implementation for compatibility

                m_TOCData.FirstTrack = 0x01;
                m_TOCData.LastTrack = 0x01; // First complete session
                datalen = SIZE_TOC_DATA;

                tocEntries = new TUSBTOCEntry[1];

                // Point A0h - First track number in program area
                tocEntries[0].ADR_Control = 0x14;
                tocEntries[0].TrackNumber = 0xA0; // Point A0
                tocEntries[0].reserved = 0x00;
                tocEntries[0].reserved2 = 0x01; // First track number
                tocEntries[0].address = 0x00;   // PMIN/PSEC/PFRAME set to zero
                datalen += SIZE_TOC_ENTRY;
                numtracks = 1;

                // More comprehensive Full TOC implementation would add:
                // Point A1h (Last track), Point A2h (Lead-out start)
            }
            else
            {

                CUETrackInfo trackInfo = GetTrackInfoForTrack(1);

                // Header
                m_TOCData.FirstTrack = 0x01;
                m_TOCData.LastTrack = 0x01; // In this format, this is the last session number
                datalen = SIZE_TOC_DATA;

                // Populate the track entries
                tocEntries = new TUSBTOCEntry[2];

                tocEntries[0].ADR_Control = 0x00;
                tocEntries[0].reserved = 0x00;
                tocEntries[0].TrackNumber = 1;
                tocEntries[0].reserved2 = 0x00;
                tocEntries[0].address = GetAddress(trackInfo.track_start, msf);
                datalen += SIZE_TOC_ENTRY;
                numtracks = 1;
                /*
                } else {
                            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Read TOC unsupported format %d", format);
                            m_nnumber_blocks = 0;  // nothing more after this send
                            m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;  // CD_CSW_STATUS_FAIL
                            m_SenseParams.bSenseKey = 0x05;
                            m_SenseParams.bAddlSenseCode = 0x24;      // Invalid Field
                            m_SenseParams.bAddlSenseCodeQual = 0x00;  // In CDB
                            SendCSW();
                    delete[] tocEntries;
                            break;
                */
            }

            // Copy the TOC header
            m_TOCData.DataLength = htons(datalen - 2);
            memcpy(m_InBuffer, &m_TOCData, SIZE_TOC_DATA);

            // Copy the TOC entries immediately after the header
            memcpy(m_InBuffer + SIZE_TOC_DATA, tocEntries, numtracks * SIZE_TOC_ENTRY);

            delete[] tocEntries;

            // Set response length
            if (allocationLength < datalen)
                datalen = allocationLength;

            m_nnumber_blocks = 0; // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, datalen);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
        }
        else
        {
            MLOGNOTE("handleSCSI READ TOC", "failed, %s", m_CDReady ? "ready" : "not ready");
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
        }
        break;
    }

    case 0x42: // READ SUB-CHANNEL CMD
    {
        unsigned int msf = (m_CBW.CBWCB[1] >> 1) & 0x01;
        // unsigned int subq = (m_CBW.CBWCB[2] >> 6) & 0x01; //TODO We're ignoring subq for now
        unsigned int parameter_list = m_CBW.CBWCB[3];
        // unsigned int track_number = m_CBW.CBWCB[6]; // Ignore track number for now. It's used only for ISRC
        int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
        int length = 0;

        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42), allocationLength = %d, msf = %u, parameter_list = 0x%02x", allocationLength, msf, parameter_list);

        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));

        if (parameter_list == 0x00)
            parameter_list = 0x01; // 0x00 is "reserved" so let's assume they want cd info

        switch (parameter_list)
        {
        // Current Position Data request
        case 0x01:
        {
            // Current Position Header
            TUSBCDSubChannelHeaderReply header;
            memset(&header, 0, SIZE_SUBCHANNEL_HEADER_REPLY);
            header.audioStatus = 0x00; // Audio status not supported
            header.dataLength = SIZE_SUBCHANNEL_01_DATA_REPLY;

            // Override audio status by querying the player
            if (cdplayer)
            {
                unsigned int state = cdplayer->GetState();
                switch (state)
                {
                case CCDPlayer::PLAYING:
                    header.audioStatus = 0x11; // Playing
                    break;
                case CCDPlayer::PAUSED:
                    header.audioStatus = 0x12; // Paused
                    break;
                case CCDPlayer::STOPPED_OK:
                    header.audioStatus = 0x13; // Stopped without error
                    break;
                case CCDPlayer::STOPPED_ERROR:
                    header.audioStatus = 0x14; // Stopped with error
                    break;
                default:
                    header.audioStatus = 0x15; // No status to return
                    break;
                }
            }

            // Current Position Data
            TUSBCDSubChannel01CurrentPositionReply data;
            memset(&data, 0, SIZE_SUBCHANNEL_01_DATA_REPLY);
            data.dataFormatCode = 0x01;

            u32 address = 0;
            if (cdplayer)
            {
                address = cdplayer->GetCurrentAddress();
                data.absoluteAddress = GetAddress(address, msf);
                CUETrackInfo trackInfo = GetTrackInfoForLBA(address);
                if (trackInfo.track_number != -1)
                {
                    data.trackNumber = trackInfo.track_number;
                    data.indexNumber = 0x01; // Assume no pregap. Perhaps we need to handle pregap?
                    data.relativeAddress = GetAddress(address - trackInfo.track_start, msf, true);
                }
            }

            // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42, 0x01) audio_status %02x, trackNumber %d, address %d, absoluteAddress %08x, relativeAddress %08x", header.audioStatus, data.trackNumber, address, data.absoluteAddress, data.relativeAddress);

            // Determine data lengths
            length = SIZE_SUBCHANNEL_HEADER_REPLY + SIZE_SUBCHANNEL_01_DATA_REPLY;

            // Copy the header & Code Page
            memcpy(m_InBuffer, &header, SIZE_SUBCHANNEL_HEADER_REPLY);
            memcpy(m_InBuffer + SIZE_SUBCHANNEL_HEADER_REPLY, &data, SIZE_SUBCHANNEL_01_DATA_REPLY);
            break;
        }

        case 0x02:
        {
            // Media Catalog Number (UPC Bar Code)
            break;
        }

        case 0x03:
        {
            // International Standard Recording Code (ISRC)
            // TODO We're ignoring track number because that's only valid here
            break;
        }

        default:
        {
            // TODO Error
        }
        }

        if (allocationLength < length)
            length = allocationLength;

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, length);

        m_nnumber_blocks = 0; // nothing more after this send
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;

        break;
    }

    case 0x52: // READ TRACK INFORMATION
    {

        // u8 open = (m_CBW.CBWCB[1] >> 2) & 0x01; unused in this command
        u8 addressType = m_CBW.CBWCB[1] & 0x03;
        u32 address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
        u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
        // u8 control = m_CBW.CBWCB[9]; unused in this command

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read Track Information");

        TUSBCDTrackInformationBlock response;
        memset(&response, 0, sizeof(TUSBCDTrackInformationBlock));
        response.dataLength = htons(46);

        switch (addressType)
        {
        case 0x00:
        {
            // Logical Block Number
            // TODO
            break;
        }
        case 0x01:
        {
            // Logical Track Number
            CUETrackInfo trackInfo = GetTrackInfoForTrack(int(address));
            response.logicalTrackNumberLSB = address & 0xff;
            response.sessionNumberLSB = 0x01; // no sessions
            if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
                response.trackMode = 0x02; // audio
            else
                response.trackMode = 0x06; // data

            response.dataMode = 0x01; // mode 1
            if (trackInfo.track_number != -1)
                response.logicalTrackStartAddress = htonl(trackInfo.track_start);
            break;
        }
        case 0x02:
        {
            // Session Number
            // TODO
            break;
        }
        }

        int length = sizeof(TUSBCDTrackInformationBlock);

        if (allocationLength < length)
            length = allocationLength;

        m_nnumber_blocks = 0; // nothing more after this send
        memcpy(m_InBuffer, &response, sizeof(TUSBCDTrackInformationBlock));
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

    case 0x4A: // GET EVENT STATUS NOTIFICATION
    {

        u8 polled = m_CBW.CBWCB[1] & 0x01;
        u8 notificationClass = m_CBW.CBWCB[4]; // This is a bitmask
        u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification");

        if (polled == 0)
        {
            // We don't support async mode
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification - we don't support async notifications");
            setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
            sendCheckCondition();
            break;
        }

        int length = 0;
        // Event Header
        TUSBCDEventStatusReplyHeader header;
        memset(&header, 0, sizeof(header));
        header.supportedEventClass = 0x10; // Only support media change events (10000b)

        // Media Change Event Request
        if (notificationClass & (1 << 4))
        {

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification - media change event response");

            // Update header
            header.eventDataLength = htons(0x04); // Always 4 because only return 1 event
            header.notificationClass = 0x04;      // 100b = media class

            // Define the event
            TUSBCDEventStatusReplyEvent event;
            memset(&event, 0, sizeof(event));

            if (discChanged)
            {
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification - sending NewMedia event");
                event.eventCode = 0x02;                  // NewMedia event
                event.data[0] = m_CDReady ? 0x02 : 0x00; // Media present : No media

                // Only clear the disc changed event if we're actually going to send the full response
                if (allocationLength >= (sizeof(TUSBCDEventStatusReplyHeader) + sizeof(TUSBCDEventStatusReplyEvent)))
                {
                    discChanged = false;
                }
            }
            else if (m_CDReady)
            {
                event.eventCode = 0x00; // No Change
                event.data[0] = 0x02;   // Media present
            }
            else
            {
                event.eventCode = 0x03; // Media Removal
                event.data[0] = 0x00;   // No media
            }

            event.data[1] = 0x00; // Reserved
            event.data[2] = 0x00; // Reserved
            memcpy(m_InBuffer + sizeof(TUSBCDEventStatusReplyHeader), &event, sizeof(TUSBCDEventStatusReplyEvent));
            length += sizeof(TUSBCDEventStatusReplyEvent);
        }
        else
        {
            // No supported event class requested
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Event Status Notification - no supported class requested");
            header.notificationClass = 0x00;
            header.eventDataLength = htons(0x00);
        }

        memcpy(m_InBuffer, &header, sizeof(TUSBCDEventStatusReplyHeader));
        length += sizeof(TUSBCDEventStatusReplyHeader);

        if (allocationLength < length)
            length = allocationLength;

        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

    case 0xAD: // READ DISC STRUCTURE aka "The Command I Was Avoiding"
    {

        // We don't advertise any "features" which should require this command
        // but certain versions of Windows e.g. Win2k sulk for a while if they
        // don't get a response. So, we're implementing bare minimum here to
        // keep them happy

        // u8 mediaType = m_CBW.CBWCB[2] && 0x0f; // unused in this command
        //  u8 mediaType = m_CBW.CBWCB[2] & 0x0f;  // unused (note: was buggy with && instead of &)
        //  u32 address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];  // unused
        // u8 layer = m_CBW.CBWCB[6];  // unused
        u8 format = m_CBW.CBWCB[7];
        u16 allocationLength = m_CBW.CBWCB[8] << 8 | (m_CBW.CBWCB[9]);
        // u8 agid = (m_CBW.CBWCB[10] >> 6) & 0x03;  // unused
        // u8 control = m_CBW.CBWCB[12];  // unused
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Read Disc Structure, format=0x%02x, allocation length is %lu, mediaType=%d", format, allocationLength, (int)m_mediaType);

        // For CD media and DVD-specific formats: return minimal empty response
        // MacOS doesn't handle CHECK CONDITION well for this command - causes USB reset
        // This is a workaround until we can properly implement stall-then-CSW sequence
        if (m_mediaType != MEDIA_TYPE::DVD &&
            (format == 0x00 || format == 0x02 || format == 0x03 || format == 0x04))
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ DISC STRUCTURE format 0x%02x for CD media - returning minimal response", format);
            // Return minimal header indicating no data available
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
            header.dataLength = __builtin_bswap16(2); // Just header, no payload (big-endian)

            int length = sizeof(TUSBCDReadDiscStructureHeader);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &header, length);
            m_nnumber_blocks = 0;
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break; // Exit case 0xAD
        }

        int length = 0;
        switch (format)
        {

        case 0x00: // Physical Format Information - DVD-specific
        case 0x02: // Disc Key Structure - DVD-specific
        case 0x03: // BCA (Burst Cutting Area) - DVD-specific
        case 0x04: // Manufacturing Information - DVD-specific
        {
            // DVD media - return minimal structure
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
            header.dataLength = 2; // just the header
            memcpy(m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
            length += sizeof(TUSBCDReadDiscStructureHeader);
            break;
        }

        case 0x01: // Copyright Information - valid for both CD and DVD
        {
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
            header.dataLength = 6;
            memcpy(m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
            length += sizeof(TUSBCDReadDiscStructureHeader);

            u8 payload[] = {
                0x00, // Copyright system type = none
                0x00, // 1 bit per region. 0x00 is region free
                0x00, // reserved
                0x00  // reserved
            };
            memcpy(m_InBuffer + sizeof(TUSBCDReadDiscStructureHeader), &payload, sizeof(payload));
            length += sizeof(payload);
            break;
        }

        default: // Empty payload
        {
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ DISC STRUCTURE unsupported format 0x%02x", format);
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
            header.dataLength = 2; // just the header
            memcpy(m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
            length += sizeof(TUSBCDReadDiscStructureHeader);
            break;
        }
        }

        // Set response length
        if (allocationLength < length)
            length = allocationLength;

        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    case 0x51: // READ DISC INFORMATION CMD
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Read Disc Information");

        // Update disc information with current media state (MacOS-compatible)
        m_DiscInfoReply.disc_status = 0x0E; // Complete disc, finalized (bits 1-0=10b), last session complete (bit 3)
        m_DiscInfoReply.first_track_number = 0x01;
        m_DiscInfoReply.number_of_sessions = 0x01; // Single session
        m_DiscInfoReply.first_track_last_session = 0x01;
        m_DiscInfoReply.last_track_last_session = GetLastTrackNumber();

        // Set disc type based on track 1 mode (MacOS uses this)
        CUETrackInfo trackInfo = GetTrackInfoForTrack(1);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            m_DiscInfoReply.disc_type = 0x00; // CD-DA (audio)
        }
        else
        {
            m_DiscInfoReply.disc_type = 0x10; // CD-ROM (data)
        }

        u32 leadoutLBA = GetLeadoutLBA();
        m_DiscInfoReply.last_lead_in_start_time = htonl(leadoutLBA);
        m_DiscInfoReply.last_possible_lead_out = htonl(leadoutLBA);

        // Set response length
        u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
        int length = sizeof(TUSBDiscInfoReply);
        if (allocationLength < length)
            length = allocationLength;

        memcpy(m_InBuffer, &m_DiscInfoReply, length);
        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    case 0x46: // Get Configuration
    {
        int rt = m_CBW.CBWCB[1] & 0x03;
        int feature = (m_CBW.CBWCB[2] << 8) | m_CBW.CBWCB[3];
        u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Get Configuration with rt = %d and feature %lu", rt, feature);

        int dataLength = 0;

        switch (rt)
        {
        case 0x00: // All features supported
        case 0x01: // All current features supported
        {
            // offset to make space for the header
            dataLength += sizeof(header);

            // Dynamic profile list based on media type
            // CD-only media: advertise ONLY CD-ROM profile (pure CD drive)
            // DVD media: advertise both DVD and CD profiles (combo drive)
            TUSBCDProfileListFeatureReply dynProfileList = profile_list;

            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                // Combo drive: advertise both profiles (8 bytes)
                dynProfileList.AdditionalLength = 0x08;
                memcpy(m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);

                // MMC spec: descending order (DVD 0x0010 before CD 0x0008)
                TUSBCProfileDescriptorReply activeDVD = dvd_profile;
                activeDVD.currentP = 0x01; // DVD IS current
                memcpy(m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
                dataLength += sizeof(activeDVD);

                TUSBCProfileDescriptorReply activeCD = cdrom_profile;
                activeCD.currentP = 0x00; // CD not current
                memcpy(m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);

                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION: DVD/CD combo drive, DVD current");
            }
            else
            {
                // CD-only drive: advertise only CD-ROM profile (4 bytes)
                dynProfileList.AdditionalLength = 0x04;
                memcpy(m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);

                TUSBCProfileDescriptorReply activeCD = cdrom_profile;
                activeCD.currentP = 0x01; // CD IS current
                memcpy(m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);

                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION: CD-ROM only drive");
            }

            memcpy(m_InBuffer + dataLength, &core, sizeof(core));
            dataLength += sizeof(core);

            memcpy(m_InBuffer + dataLength, &morphing, sizeof(morphing));
            dataLength += sizeof(morphing);

            memcpy(m_InBuffer + dataLength, &mechanism, sizeof(mechanism));
            dataLength += sizeof(mechanism);

            memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
            dataLength += sizeof(multiread);

            // For DVD media, add DVD Read feature instead of/in addition to CD Read
            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                memcpy(m_InBuffer + dataLength, &dvdread, sizeof(dvdread));
                dataLength += sizeof(dvdread);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Sending DVD-Read feature (0x001f)", rt);
            }
            else
            {
                memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                dataLength += sizeof(cdread);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Sending CD-Read feature (0x001e), mediaType=%d", rt, (int)m_mediaType);
            }

            memcpy(m_InBuffer + dataLength, &powermanagement, sizeof(powermanagement));
            dataLength += sizeof(powermanagement);

            memcpy(m_InBuffer + dataLength, &audioplay, sizeof(audioplay));
            dataLength += sizeof(audioplay);

            // Set header profile and copy to buffer
            TUSBCDFeatureHeaderReply dynHeader = header;
            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_DVD_ROM (0x0010)", rt);
            }
            else
            {
                dynHeader.currentProfile = htons(PROFILE_CDROM);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_CDROM (0x0008)", rt);
            }
            dynHeader.dataLength = htonl(dataLength - 4);
            memcpy(m_InBuffer, &dynHeader, sizeof(dynHeader));

            break;
        }

        case 0x02: // starting at the feature requested
        {
            // Offset for header
            dataLength += sizeof(header);

            switch (feature)
            {
            case 0x00:
            { // Profile list
                // Dynamic profile list: CD-only for CDs, combo for DVDs
                TUSBCDProfileListFeatureReply dynProfileList = profile_list;

                if (m_mediaType == MEDIA_TYPE::DVD)
                {
                    // Combo drive: both profiles
                    dynProfileList.AdditionalLength = 0x08;
                    memcpy(m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                    dataLength += sizeof(dynProfileList);

                    TUSBCProfileDescriptorReply activeDVD = dvd_profile;
                    activeDVD.currentP = 0x01;
                    memcpy(m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
                    dataLength += sizeof(activeDVD);

                    TUSBCProfileDescriptorReply activeCD = cdrom_profile;
                    activeCD.currentP = 0x00;
                    memcpy(m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                    dataLength += sizeof(activeCD);

                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02, feat 0x00): DVD/CD combo, DVD current");
                }
                else
                {
                    // CD-only drive: only CD profile
                    dynProfileList.AdditionalLength = 0x04;
                    memcpy(m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                    dataLength += sizeof(dynProfileList);

                    TUSBCProfileDescriptorReply activeCD = cdrom_profile;
                    activeCD.currentP = 0x01;
                    memcpy(m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                    dataLength += sizeof(activeCD);

                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02, feat 0x00): CD-ROM only drive (profile 0x0008, current=%d, length=0x%02x)",
                                    activeCD.currentP, dynProfileList.AdditionalLength);
                }
                break;
            }

            case 0x01:
            { // Core
                memcpy(m_InBuffer + dataLength, &core, sizeof(core));
                dataLength += sizeof(core);
                break;
            }

            case 0x02:
            { // Morphing
                memcpy(m_InBuffer + dataLength, &morphing, sizeof(morphing));
                dataLength += sizeof(morphing);
                break;
            }

            case 0x03:
            { // Removable Medium
                memcpy(m_InBuffer + dataLength, &mechanism, sizeof(mechanism));
                dataLength += sizeof(mechanism);
                break;
            }

            case 0x1d:
            { // Multiread
                memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
                dataLength += sizeof(multiread);
                break;
            }

            case 0x1e:
            { // CD-Read
                if (m_mediaType == MEDIA_TYPE::CD)
                {
                    memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                    dataLength += sizeof(cdread);
                }
                break;
            }

            case 0x1f:
            { // DVD-Read
                if (m_mediaType == MEDIA_TYPE::DVD)
                {
                    memcpy(m_InBuffer + dataLength, &dvdread, sizeof(dvdread));
                    dataLength += sizeof(dvdread);
                }
                break;
            }

            case 0x100:
            { // Power Management
                memcpy(m_InBuffer + dataLength, &powermanagement, sizeof(powermanagement));
                dataLength += sizeof(powermanagement);
                break;
            }

            case 0x103:
            { // Analogue Audio Play
                memcpy(m_InBuffer + dataLength, &audioplay, sizeof(audioplay));
                dataLength += sizeof(audioplay);
                break;
            }

            default:
            {
                // Log unhandled feature requests to identify what macOS is querying
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02): Unhandled feature 0x%04x requested", feature);
                break;
            }
            }

            // Set header profile and copy to buffer
            TUSBCDFeatureHeaderReply dynHeader = header;
            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02): Returning PROFILE_DVD_ROM (0x0010)");
            }
            else
            {
                dynHeader.currentProfile = htons(PROFILE_CDROM);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02): Returning PROFILE_CDROM (0x0008)");
            }
            dynHeader.dataLength = htonl(dataLength - 4);
            memcpy(m_InBuffer, &dynHeader, sizeof(dynHeader));
            break;
        }
        }

        // Set response length
        if (allocationLength < dataLength)
            dataLength = allocationLength;

        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, dataLength);
        m_nState = TCDState::DataIn;
        // m_CSW.bmCSWStatus = bmCSWStatus;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

    case 0x4B: // PAUSE/RESUME
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PAUSE/RESUME");
        int resume = m_CBW.CBWCB[8] & 0x01;

        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {
            if (resume)
                cdplayer->Resume();
            else
                cdplayer->Pause();
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x2B: // SEEK
    {

        // Where to start reading (LBA)
        m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SEEK to LBA %lu", m_nblock_address);

        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {
            cdplayer->Seek(m_nblock_address);
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x47: // PLAY AUDIO MSF
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

        CUETrackInfo trackInfo = GetTrackInfoForLBA(start_lba);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            // Play the audio
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CD Player found, sending command");
            CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer)
            {
                if (start_lba == 0xFFFFFFFF)
                {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CD Player found, Resume");
                    cdplayer->Resume();
                }
                else if (start_lba == end_lba)
                {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CD Player found, Pause");
                    cdplayer->Pause();
                }
                else
                {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "CD Player found, Play");
                    cdplayer->Play(start_lba, num_blocks);
                }
            }
        }
        else
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO MSF: Not an audio track");
            bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
            m_SenseParams.bSenseKey = 0x05;
            m_SenseParams.bAddlSenseCode = 0x64; // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
            m_SenseParams.bAddlSenseCodeQual = 0x00;
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x4E: // STOP / SCAN
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "STOP / SCAN");

        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {
            cdplayer->Pause();
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x45: // PLAY AUDIO (10)
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10)");

        // Where to start reading (LBA)
        m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

        // Number of blocks to read (LBA)
        m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

        // Play the audio, but only if length > 0
        if (m_nnumber_blocks > 0)
        {
            CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
            if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
            {
                CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
                if (cdplayer)
                {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Play command sent");
                    if (m_nblock_address == 0xffffffff)
                        cdplayer->Resume();
                    else
                        cdplayer->Play(m_nblock_address, m_nnumber_blocks);
                }
            }
            else
            {
                bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
                m_SenseParams.bSenseKey = 0x05;
                m_SenseParams.bAddlSenseCode = 0x64; // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
                m_SenseParams.bAddlSenseCodeQual = 0x00;
            }
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0xA5: // PLAY AUDIO (12)
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12)");

        // Where to start reading (LBA)
        m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

        // Number of blocks to read (LBA)
        m_nnumber_blocks = (u32)(m_CBW.CBWCB[6] << 24) | (u32)(m_CBW.CBWCB[7] << 16) | (u32)(m_CBW.CBWCB[8] << 8) | m_CBW.CBWCB[9];

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

        // Play the audio, but only if length > 0
        if (m_nnumber_blocks > 0)
        {
            CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
            if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
            {
                CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
                if (cdplayer)
                {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12) Play command sent");
                    if (m_nblock_address == 0xffffffff)
                        cdplayer->Resume();
                    else
                        cdplayer->Play(m_nblock_address, m_nnumber_blocks);
                }
            }
            else
            {
                bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
                m_SenseParams.bSenseKey = 0x05;
                m_SenseParams.bAddlSenseCode = 0x64; // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
                m_SenseParams.bAddlSenseCodeQual = 0x00;
            }
        }

        m_CSW.bmCSWStatus = bmCSWStatus;
        SendCSW();
        break;
    }

    case 0x55: // Mode Select (10)
    {
        u16 transferLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Select (10), transferLength is %u", transferLength);

        // Read the data from the host but don't do anything with it (yet!)
        m_nState = TCDState::DataOut;
        m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataOut,
                                    m_OutBuffer, transferLength);

        // Unfortunately the payload doesn't arrive here. Check out the
        // ProcessOut method for payload processing

        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    // We only need this because MacOS is a problem child
    case 0x1a: // Mode Sense (6)
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6)");
        // int DBD = (m_CBW.CBWCB[1] >> 3) & 0x01; // We don't implement block descriptors
        int page_control = (m_CBW.CBWCB[2] >> 6) & 0x03;
        int page = m_CBW.CBWCB[2] & 0x3f;
        // int sub_page_code = m_CBW.CBWCB[3];
        int allocationLength = m_CBW.CBWCB[4];
        // int control = m_CBW.CBWCB[5];  // unused

        int length = 0;

        // We don't support saved values
        if (page_control == 0x03)
        {
            bmCSWStatus = CD_CSW_STATUS_FAIL;    // CD_CSW_STATUS_FAIL
            m_SenseParams.bSenseKey = 0x05;      // Illegal Request
            m_SenseParams.bAddlSenseCode = 0x39; // Saving parameters not supported
            m_SenseParams.bAddlSenseCodeQual = 0x00;
        }
        else
        {

            // Define our response
            ModeSense6Header reply_header;
            memset(&reply_header, 0, sizeof(reply_header));
            reply_header.mediumType = GetMediumType();

            switch (page)
            {

            case 0x3f: // This required all mode pages
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x3f: All Mode Pages");
            // Fall through...
            case 0x01:
            {
                // Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x01 response");

                // Define our Code Page
                ModePage0x01Data codepage;
                memset(&codepage, 0, sizeof(codepage));

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x1a:
            {
                // Mode Page 0x1A (Power Condition)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x1a response");

                // Define our Code Page
                ModePage0x1AData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x1a;
                codepage.pageLength = 0x0a;

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x2a:
            {
                // Mode Page 0x2A (MM Capabilities and Mechanical Status) Data
                ModePage0x2AData codepage;
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x2a response");
                FillModePage2A(codepage);
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x0e:
            {
                // Mode Page 0x0E (CD Audio Control Page)
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x0e response");

                CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
                u8 volume = 0xff;
                if (cdplayer)
                {
                    // When we return real volume, games that allow volume control don't send proper volume levels
                    // but when we hard code this to 0xff, everything seems to work fine. Weird.
                    // volume = cdplayer->GetVolume();
                    volume = 0xff;
                }

                // Define our Code Page
                ModePage0x0EData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x0e;
                codepage.pageLength = 16;
                codepage.IMMEDAndSOTC = 0x04;
                codepage.CDDAOutput0Select = 0x01; // audio channel 0
                codepage.Output0Volume = volume;
                codepage.CDDAOutput1Select = 0x02; // audio channel 1
                codepage.Output1Volume = volume;
                codepage.CDDAOutput2Select = 0x00; // none
                codepage.Output2Volume = 0x00;     // muted
                codepage.CDDAOutput3Select = 0x00; // none
                codepage.Output3Volume = 0x00;     // muted

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                break;
            }

            default:
            {
                // We don't support this code page
                bmCSWStatus = CD_CSW_STATUS_FAIL;    // CD_CSW_STATUS_FAIL
                m_SenseParams.bSenseKey = 0x05;      // Illegal Request
                m_SenseParams.bAddlSenseCode = 0x24; // INVALID FIELD IN COMMAND PACKET
                m_SenseParams.bAddlSenseCodeQual = 0x00;
                break;
            }
            }

            reply_header.modeDataLength = htons(length - 1);
            memcpy(m_InBuffer, &reply_header, sizeof(reply_header));
        }

        // Trim the reply length according to what the host requested
        if (allocationLength < length)
            length = allocationLength;

        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6), Sending response with length %d", length);

        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    case 0x5a: // Mode Sense (10)
    {
        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10)");

        int LLBAA = (m_CBW.CBWCB[1] >> 7) & 0x01; // We don't support this
        int DBD = (m_CBW.CBWCB[1] >> 6) & 0x01;   // TODO: Implement this!
        int page = m_CBW.CBWCB[2] & 0x3F;
        int page_control = (m_CBW.CBWCB[2] >> 6) & 0x03;
        u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) with LLBAA = %d, DBD = %d, page = %02x, allocationLength = %lu", LLBAA, DBD, page, allocationLength);

        int length = 0;

        // We don't support saved values
        if (page_control == 0x03)
        {
            // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) page_control=0x03 (saved) not supported");
            bmCSWStatus = CD_CSW_STATUS_FAIL;    // CD_CSW_STATUS_FAIL
            m_SenseParams.bSenseKey = 0x05;      // Illegal Request
            m_SenseParams.bAddlSenseCode = 0x39; // Saving parameters not supported
            m_SenseParams.bAddlSenseCodeQual = 0x00;
        }
        else
        {
            // Define our response
            ModeSense10Header reply_header;
            memset(&reply_header, 0, sizeof(reply_header));
            reply_header.mediumType = GetMediumType();
            length += sizeof(reply_header);

            switch (page)
            {
            case 0x3f: // This required all mode pages
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x3f: All Mode Pages");
                // Fall through...
            case 0x01:
            {
                // Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x01 response");

                // Define our Code Page
                ModePage0x01Data codepage;
                memset(&codepage, 0, sizeof(codepage));

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

                // In case 0x5a, add new page:
            case 0x0D:
            { // CD Device Parameters
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                                "MODE SENSE(10) Page 0x0D (CD Device Parameters)");

                struct CDDeviceParametersPage
                {
                    u8 pageCode;   // 0x0D
                    u8 pageLength; // 0x06
                    u8 reserved1;
                    u8 inactivityTimer; // Minutes before standby
                    u16 secondsPerMSF;  // S/MSF units per second
                    u16 framesPerMSF;   // F/MSF units per second
                } PACKED;

                CDDeviceParametersPage codePage = {0};
                codePage.pageCode = 0x0D;
                codePage.pageLength = 0x06;
                codePage.inactivityTimer = 0x00;    // No auto-standby
                codePage.secondsPerMSF = htons(60); // 60 S units per second
                codePage.framesPerMSF = htons(75);  // 75 F units per second

                // memcpy(m_InBuffer + length, &codepage, sizeof(codepage));

                memcpy(m_InBuffer + length, &codePage, sizeof(codePage));
                length += sizeof(codePage);
                break;
            }

            case 0x1a:
            {
                // Mode Page 0x1A (Power Condition)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x1a response");

                // Define our Code Page
                ModePage0x1AData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x1a;
                codepage.pageLength = 0x0a;

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x2a:
            {
                // Mode Page 0x2A (MM Capabilities and Mechanical Status) Data
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x2a response");

                // Use shared code 0x2A filling function
                ModePage0x2AData codepage;
                FillModePage2A(codepage);
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x0e:
            {
                // Mode Page 0x0E (CD Audio Control Page)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x0e response");

                CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
                u8 volume = 0xff;
                if (cdplayer)
                {
                    // When we return real volume, games that allow volume control don't send proper volume levels
                    // but when we hard code this to 0xff, everything seems to work fine. Weird.
                    // volume = cdplayer->GetVolume();
                    volume = 0xff;
                }

                // Define our Code Page
                ModePage0x0EData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x0e;
                codepage.pageLength = 16;
                codepage.IMMEDAndSOTC = 0x04;
                codepage.CDDAOutput0Select = 0x01; // audio channel 0
                codepage.Output0Volume = volume;
                codepage.CDDAOutput1Select = 0x02; // audio channel 1
                codepage.Output1Volume = volume;
                codepage.CDDAOutput2Select = 0x00; // none
                codepage.Output2Volume = 0x00;     // muted
                codepage.CDDAOutput3Select = 0x00; // none
                codepage.Output3Volume = 0x00;     // muted

                // Copy the header & Code Page
                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                break;
            }

            default:
            {
                // We don't support this code page
                // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) unsupported page 0x%02x", page);
                bmCSWStatus = CD_CSW_STATUS_FAIL;    // CD_CSW_STATUS_FAIL
                m_SenseParams.bSenseKey = 0x05;      // Illegal Request
                m_SenseParams.bAddlSenseCode = 0x24; // INVALID FIELD IN COMMAND PACKET
                m_SenseParams.bAddlSenseCodeQual = 0x00;
                break;
            }
            }

            reply_header.modeDataLength = htons(length - 2);
            memcpy(m_InBuffer, &reply_header, sizeof(reply_header));
        }

        // Trim the reply length according to what the host requested
        if (allocationLength < length)
            length = allocationLength;

        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10), Sending response with length %d", length);

        m_nnumber_blocks = 0; // nothing more after this send
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;
        break;
    }

    case 0xAC: // GET PERFORMANCE
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "GET PERFORMANCE (0xAC)");

        u8 getPerformanceStub[20] = {
            0x00, 0x00, 0x00, 0x10, // Header: Length = 16 bytes (descriptor)
            0x00, 0x00, 0x00, 0x00, // Reserved or Start LBA
            0x00, 0x00, 0x00, 0x00, // Reserved or End LBA
            0x00, 0x00, 0x00, 0x01, // Performance metric (e.g. 1x speed)
            0x00, 0x00, 0x00, 0x00  // Additional reserved
        };

        memcpy(m_InBuffer, getPerformanceStub, sizeof(getPerformanceStub));

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, sizeof(getPerformanceStub));
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = bmCSWStatus;

        break;
    }

    case 0xa4: // Weird thing from Windows 2000
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "A4 from Win2k");

        // Response copied from an ASUS CDROM drive. It seems to know
        // what this is, so let's just copy it
        u8 response[] = {0x0, 0x6, 0x0, 0x0, 0x25, 0xff, 0x1, 0x0};

        memcpy(m_InBuffer, response, sizeof(response));

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, sizeof(response));
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

        // SCSI TOOLBOX
    case 0xD9: // LIST DEVICES
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SCSITB List Devices");

        // First device is CDROM and the other are not implemented
        u8 devices[] = {0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

        memcpy(m_InBuffer, devices, sizeof(devices));

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, sizeof(devices));
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

    case 0xD2: // NUMBER OF FILES
    case 0xDA: // NUMBER OF CDS
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SCSITB Number of Files/CDs");

        SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));

        // SCSITB defines max entries as 100
        const size_t MAX_ENTRIES = 100;
        size_t count = scsitbservice->GetCount();
        if (count > MAX_ENTRIES)
            count = MAX_ENTRIES;

        u8 num = (u8)count;

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SCSITB Discovered %d Files/CDs", num);

        memcpy(m_InBuffer, &num, sizeof(num));

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, sizeof(num));
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

    case 0xD0: // LIST FILES
    case 0xD7: // LIST CDS
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SCSITB List Files/CDs");

        SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));

        // SCSITB defines max entries as 100
        const size_t MAX_ENTRIES = 100;
        size_t count = scsitbservice->GetCount();
        if (count > MAX_ENTRIES)
            count = MAX_ENTRIES;

        TUSBCDToolboxFileEntry *entries = new TUSBCDToolboxFileEntry[MAX_ENTRIES];
        for (u8 i = 0; i < count; ++i)
        {
            TUSBCDToolboxFileEntry *entry = &entries[i];
            entry->index = i;
            entry->type = 0; // file type

            // Copy name capped to 32 chars + NUL
            const char *name = scsitbservice->GetName(i);
            size_t j = 0;
            for (; j < 32 && name[j] != '\0'; ++j)
            {
                entry->name[j] = (u8)name[j];
            }
            entry->name[j] = 0; // null terminate

            // Get size and store as 40-bit big endian (highest byte zero)
            DWORD size = scsitbservice->GetSize(i);
            entry->size[0] = 0;
            entry->size[1] = (size >> 24) & 0xFF;
            entry->size[2] = (size >> 16) & 0xFF;
            entry->size[3] = (size >> 8) & 0xFF;
            entry->size[4] = size & 0xFF;
        }

        memcpy(m_InBuffer, entries, count * sizeof(TUSBCDToolboxFileEntry));

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, count * sizeof(TUSBCDToolboxFileEntry));
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

        delete[] entries;

        break;
    }

    case 0xD8: // SET NEXT CD
    {

        int index = m_CBW.CBWCB[1];
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SET NEXT CD index %d", index);

        // TODO set bounds checking here and throw check condition if index is not valid
        // currently, it will silently ignore OOB indexes

        SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));
        scsitbservice->SetNextCD(index);

        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        SendCSW();
        break;
    }

    default:
    {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Unknown SCSI Command is 0x%02x", m_CBW.CBWCB[0]);
        setSenseData(0x05, 0x20, 0x00); // INVALID COMMAND OPERATION CODE
        sendCheckCondition();
        break;
    }
    }

    // Reset the status
    // bmCSWStatus = CD_CSW_STATUS_OK;
}

// this function is called periodically from task level for IO
//(IO must not be attempted in functions called from IRQ)
void CUSBCDGadget::Update()
{
    // MLOGDEBUG ("CUSBCDGadget::Update", "entered skip=%u, transfer=%u", skip_bytes, transfer_block_size);
    switch (m_nState)
    {
    case TCDState::DataInRead:
    {
        u64 offset = 0;
        int readCount = 0;
        if (m_CDReady)
        {

            CDROM_DEBUG_LOG("UpdateRead", "Seek to %lu", block_size * m_nblock_address);
            offset = m_pDevice->Seek(block_size * m_nblock_address);
            if (offset != (u64)(-1))
            {
                // Cap at MAX_BLOCKS_READ blocks. This is what a READ CD request will
                // require any excess blocks will be read next time around this loop
                u32 blocks_to_read_in_batch = m_nnumber_blocks;
                if (blocks_to_read_in_batch > MaxBlocksToRead)
                {
                    blocks_to_read_in_batch = MaxBlocksToRead;
                    m_nnumber_blocks -= MaxBlocksToRead; // Update remaining for subsequent reads if needed
                    MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is %lu", blocks_to_read_in_batch, m_nnumber_blocks);
                }
                else
                {
                    MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is now zero", blocks_to_read_in_batch);
                    m_nnumber_blocks = 0;
                }

                // Calculate total size of the batch read
                u32 total_batch_size = blocks_to_read_in_batch * block_size;

                CDROM_DEBUG_LOG("UpdateRead", "Starting batch read for %lu blocks (total %lu bytes)", blocks_to_read_in_batch, total_batch_size);
                // Perform the single large read
                readCount = m_pDevice->Read(m_FileChunk, total_batch_size);
                CDROM_DEBUG_LOG("UpdateRead", "Read %d bytes in batch", readCount);

                if (readCount < static_cast<int>(total_batch_size))
                {
                    // Handle error: partial read
                    setSenseData(0x04, 0x11, 0x00); // UNRECOVERED READ ERROR
                    sendCheckCondition();
                    return; // Exit if read failed
                }

                u8 *dest_ptr = m_InBuffer; // Pointer to current write position in m_InBuffer
                u32 total_copied = 0;

                // Iterate through the *read data* in memory
                // TODO Optimization, if transfer_block_size and block_size are the same, and
                // skip_bytes is zero, we can just copy without looping
                for (u32 i = 0; i < blocks_to_read_in_batch; ++i)
                {
                    if (transfer_block_size > block_size)
                    {
                        // We've been asked to return more bytes than we've read from
                        // the underlying image. We have to generate some bytes
                        //
                        // This is all a bit shonky for now :O

                        u8 sector2352[2352] = {0};

                        int offset = 0;

                        // SYNC (12 bytes)
                        if (mcs & 0x10)
                        {
                            memset(sector2352 + offset, 0x00, 1);      // 0x00
                            memset(sector2352 + offset + 1, 0xFF, 10); // 0xFF * 10
                            sector2352[offset + 11] = 0x00;            // 0x00
                            offset += 12;
                        }

                        // HEADER (4 bytes)
                        if (mcs & 0x08)
                        {
                            u32 lba = m_nblock_address + i;
                            lba += 150; // the 2 sec nonesense
                            u8 minutes = lba / (75 * 60);
                            u8 seconds = (lba / 75) % 60;
                            u8 frames = lba % 75;

                            sector2352[offset + 0] = minutes; // MSF Minute
                            sector2352[offset + 1] = seconds; // MSF Second
                            sector2352[offset + 2] = frames;  // MSF Frame
                            sector2352[offset + 3] = 0x01;    // Mode 1
                            offset += 4;
                        }

                        // USER DATA (2048 bytes)
                        if (mcs & 0x04)
                        {
                            u8 *current_block_start = m_FileChunk + (i * block_size);
                            memcpy(sector2352 + offset, current_block_start, 2048);
                            offset += 2048;
                        }

                        // EDC/ECC (remaining bytes)
                        if (mcs & 0x02)
                        {
                            // Mode 1 has 288 ECC bytes at end. For now
                            // we'll send zeros and hope the host ignores it
                            memset(sector2352 + offset, 0x00, 288);
                            offset += 288;
                        }

                        memcpy(dest_ptr, sector2352 + skip_bytes, transfer_block_size);
                    }
                    else
                    {
                        // Calculate the starting point for the current block within the m_FileChunk
                        u8 *current_block_start = m_FileChunk + (i * block_size);

                        // Copy only the portion after skip_bytes into the destination buffer
                        memcpy(dest_ptr, current_block_start + skip_bytes, transfer_block_size);
                    }
                    dest_ptr += transfer_block_size;
                    total_copied += transfer_block_size;
                }
                // Update m_nblock_address after the batch read
                m_nblock_address += blocks_to_read_in_batch;

                // Adjust m_nbyteCount based on how many bytes were copied
                m_nbyteCount -= total_copied;
                m_nState = TCDState::DataIn;

                // Begin USB transfer of the in-buffer (only valid data)
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, total_copied);
            }
        }
        if (!m_CDReady || offset == (u64)(-1))
        {
            MLOGERR("UpdateRead", "failed, %s, offset=%llu",
                    m_CDReady ? "ready" : "not ready", offset);
            m_CSW.bmCSWStatus = CD_CSW_STATUS_PHASE_ERR;
            m_SenseParams.bSenseKey = 0x02;          // Not Ready
            m_SenseParams.bAddlSenseCode = 0x04;     // LOGICAL UNIT NOT READY
            m_SenseParams.bAddlSenseCodeQual = 0x00; // CAUSE NOT REPORTABLE
            SendCSW();
        }
        break;
    }

    case 0xBD: // MECHANISM STATUS
    {
        u16 allocationLength = (m_CBW.CBWCB[8] << 8) | m_CBW.CBWCB[9];

        struct MechanismStatus
        {
            u8 fault : 1;           // bit 0
            u8 changer_state : 2;   // bits 2-1
            u8 current_slot : 5;    // bits 7-3
            u8 mechanism_state : 5; // bits 4-0 of byte 1
            u8 door_open : 1;       // bit 4 of byte 1
            u8 reserved1 : 2;       // bits 7-6
            u8 current_lba[3];      // bytes 2-4 (24-bit LBA)
            u8 num_slots;           // byte 5
            u16 slot_table_length;  // bytes 6-7
        } PACKED;

        MechanismStatus status = {0};
        status.fault = 0;
        status.changer_state = 0;      // No changer
        status.current_slot = 0;       // Slot 0
        status.mechanism_state = 0x00; // Idle
        status.door_open = 0;          // Door closed (tray loaded)
        status.num_slots = 1;          // Single slot device
        status.slot_table_length = 0;  // No slot table

        int length = sizeof(MechanismStatus);
        if (allocationLength < length)
            length = allocationLength;

        memcpy(m_InBuffer, &status, length);
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, length);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        break;
    }

        /*
                case TCDState::DataOutWrite:
                        {
                                //process block from host
                                assert(m_nnumber_blocks>0);
                                u64 offset=0;
                                int writeCount=0;
                                if(m_CDReady)
                                {
                                        offset=m_pDevice->Seek(BLOCK_SIZE*m_nblock_address);
                                        if(offset!=(u64)(-1))
                                        {
                                                writeCount=m_pDevice->Write(m_OutBuffer,BLOCK_SIZE);
                                        }
                                        if(writeCount>0)
                                        {
                                                if(writeCount<BLOCK_SIZE)
                                                {
                                                        MLOGERR("UpdateWrite","writeCount = %u ",writeCount);
                                                        m_CSW.bmCSWStatus=CD_CSW_STATUS_FAIL;
                                                        m_ReqSenseReply.bSenseKey = 0x2;
                                                        m_ReqSenseReply.bAddlSenseCode = 0x1;
                                                        SendCSW();
                                                        break;
                                                }
                                                m_nnumber_blocks--;
                                                m_nblock_address++;
                                                if(m_nnumber_blocks==0)  //done receiving data from host
                                                {
                                                        SendCSW();
                                                        break;
                                                }
                                        }
                                }
                                if(!m_CDReady || offset==(u64)(-1) || writeCount<=0)
                                {
                                        MLOGERR("UpdateWrite","failed, %s, offset=%i, writeCount=%i",
                                                m_CDReady?"ready":"not ready",offset,writeCount);
                                        m_CSW.bmCSWStatus=CD_CSW_STATUS_FAIL;
                                        m_ReqSenseReply.bSenseKey = 2;
                                        m_ReqSenseReply.bAddlSenseCode = 1;
                                        SendCSW();
                                        break;
                                }
                                else
                                {
                                        if(m_nnumber_blocks>0)  //get next block
                                        {
                                                m_pEP[EPOut]->BeginTransfer(
                                                        CUSBCDGadgetEndpoint::TransferDataOut,
                                                        m_OutBuffer,512);
                                                m_nState=TCDState::DataOut;
                                        }
                                }
                                break;
                        }
        */

    default:
        break;
    }
}
