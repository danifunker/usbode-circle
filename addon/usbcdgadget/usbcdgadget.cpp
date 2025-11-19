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
#include <circle/synchronize.h>

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

CUSBCDGadget::CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, IImageDevice *pDevice)
    : CDWUSBGadget(pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr}
{
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget",
             "=== CONSTRUCTOR === pDevice=%p, isFullSpeed=%d", pDevice, isFullSpeed);
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
            CDROM_DEBUG_LOG("CUSBCDGadget::CUSBCDGadget", "CD-ROM debug logging enabled");
        }
    }
    else
    {
        m_bDebugLogging = false; // Default to disabled if config service not available
    }

    if (pDevice)
    {
        MLOGNOTE("CUSBCDGadget::CUSBCDGadget",
                 "Constructor calling SetDevice()...");

        SetDevice(pDevice);
    }
    else
    {
        MLOGNOTE("CUSBCDGadget::CUSBCDGadget",
                 "Constructor: No initial device provided");
    }
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget",
             "=== CONSTRUCTOR EXIT === m_CDReady=%d, mediaState=%d",
             m_CDReady, (int)m_mediaState);
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
void CUSBCDGadget::SetDevice(IImageDevice *dev)
{
    MLOGNOTE("CUSBCDGadget::SetDevice",
             "=== ENTRY === dev=%p, m_pDevice=%p, m_nState=%d",
             dev, m_pDevice, (int)m_nState);

    // Hand the new device to the CD Player
    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "Passed CueBinFileDevice to cd player");
    }

    // Are we changing the device on an already-active USB connection?
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

    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();

    // Only set media ready if this is a disc swap
    // Initial load will be handled by OnActivate() when USB becomes active
    if (bDiscSwap)
    {
        m_CDReady = true;
        m_mediaState = MediaState::MEDIUM_PRESENT_UNIT_ATTENTION;
        m_SenseParams.bSenseKey = 0x06;
        m_SenseParams.bAddlSenseCode = 0x28;
        m_SenseParams.bAddlSenseCodeQual = 0x00;
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        discChanged = true;

        MLOGNOTE("CUSBCDGadget::SetDevice",
                 "Disc swap: Set UNIT_ATTENTION, sense=06/28/00");
    }
    else
    {
        // Initial load - leave NOT READY, OnActivate will handle it
        MLOGNOTE("CUSBCDGadget::SetDevice",
                 "Initial load: Deferring media ready state to OnActivate()");
    }

    MLOGNOTE("CUSBCDGadget::SetDevice",
             "=== EXIT === m_CDReady=%d, mediaState=%d, sense=%02x/%02x/%02x",
             m_CDReady, (int)m_mediaState,
             m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);
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

// Functions inspiried by bluescsi v2
// Helper function for TOC entry formatting
void CUSBCDGadget::FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0; // Reserved
    dest[1] = control_adr;
    dest[2] = track->track_number;
    dest[3] = 0; // Reserved

    if (use_MSF)
    {
        dest[4] = 0;
        LBA2MSF(track->data_start, &dest[5], false);
    }
    else
    {
        dest[4] = (track->data_start >> 24) & 0xFF;
        dest[5] = (track->data_start >> 16) & 0xFF;
        dest[6] = (track->data_start >> 8) & 0xFF;
        dest[7] = (track->data_start >> 0) & 0xFF;
    }
}

// Helper function for Raw TOC entry formatting
void CUSBCDGadget::FormatRawTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool useBCD)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0x01; // Session always 1
    dest[1] = control_adr;
    dest[2] = 0x00;                // TNO, always 0
    dest[3] = track->track_number; // POINT
    dest[4] = 0x00;                // ATIME (unused)
    dest[5] = 0x00;
    dest[6] = 0x00;
    dest[7] = 0; // HOUR

    if (useBCD)
    {
        LBA2MSFBCD(track->data_start, &dest[8], false);
    }
    else
    {
        LBA2MSF(track->data_start, &dest[8], false);
    }
}

// Complete READ TOC handler
void CUSBCDGadget::DoReadTOC(bool msf, uint8_t startingTrack, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG("DoReadTOC", "Entry: msf=%d, startTrack=%d, allocLen=%d", msf, startingTrack, allocationLength);

    // NO SPECIAL CASE FOR 0xAA - let it flow through normally

    // Format track info
    uint8_t *trackdata = &m_InBuffer[4];
    int trackcount = 0;
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};

    CDROM_DEBUG_LOG("DoReadTOC", "Building track list");
    cueParser.restart();
    const CUETrackInfo *trackinfo;
    while ((trackinfo = cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
            firsttrack = trackinfo->track_number;
        lasttrack = *trackinfo;

        // Include tracks >= startingTrack
        // Since 0xAA (170) is > any track number (1-99), this will SKIP all tracks when startingTrack=0xAA
        if (startingTrack == 0 || startingTrack <= trackinfo->track_number)
        {
            FormatTOCEntry(trackinfo, &trackdata[8 * trackcount], msf);

            CDROM_DEBUG_LOG("DoReadTOC", "  Track %d: mode=%d, start=%u, msf=%d",
                            trackinfo->track_number, trackinfo->track_mode,
                            trackinfo->data_start, msf);

            trackcount++;
        }
    }

    // ALWAYS add leadout when startingTrack is 0 OR when we want tracks from startingTrack onwards
    // For startingTrack=0xAA: trackcount will be 0 here (no regular tracks added)
    // For startingTrack=0: trackcount will be 10 (all tracks added)
    CUETrackInfo leadout = {};
    leadout.track_number = 0xAA;
    leadout.track_mode = (lasttrack.track_number != 0) ? lasttrack.track_mode : CUETrack_MODE1_2048;
    leadout.data_start = GetLeadoutLBA();

    // Add leadout to the TOC
    FormatTOCEntry(&leadout, &trackdata[8 * trackcount], msf);

    CDROM_DEBUG_LOG("DoReadTOC", "  Lead-out: LBA=%u", leadout.data_start);
    trackcount++;

    // Format header
    uint16_t toc_length = 2 + trackcount * 8;
    m_InBuffer[0] = (toc_length >> 8) & 0xFF;
    m_InBuffer[1] = toc_length & 0xFF;
    m_InBuffer[2] = firsttrack;
    m_InBuffer[3] = lasttrack.track_number;

    CDROM_DEBUG_LOG("DoReadTOC", "Header: Length=%d, First=%d, Last=%d, Tracks=%d",
                    toc_length, firsttrack, lasttrack.track_number, trackcount);

    // Validation: when startingTrack is specified (not 0), we need at least the leadout
    if (startingTrack != 0 && startingTrack != 0xAA && trackcount < 2)
    {
        CDROM_DEBUG_LOG("DoReadTOC", "INVALID: startTrack=%d but trackcount=%d", startingTrack, trackcount);
        setSenseData(0x05, 0x24, 0x00);
        sendCheckCondition();
        return;
    }

    uint32_t len = 2 + toc_length;
    if (len > allocationLength)
        len = allocationLength;

    // LOG RESPONSE BUFFER
    CDROM_DEBUG_LOG("DoReadTOC", "Response (%d bytes, %d requested, full_size=%d):",
                    len, allocationLength, 2 + toc_length);
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG("DoReadTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, m_InBuffer[i + 0], m_InBuffer[i + 1], m_InBuffer[i + 2], m_InBuffer[i + 3],
                            m_InBuffer[i + 4], m_InBuffer[i + 5], m_InBuffer[i + 6], m_InBuffer[i + 7],
                            m_InBuffer[i + 8], m_InBuffer[i + 9], m_InBuffer[i + 10], m_InBuffer[i + 11],
                            m_InBuffer[i + 12], m_InBuffer[i + 13], m_InBuffer[i + 14], m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG("DoReadTOC", "%s", buf);
        }
    }

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadSessionInfo(bool msf, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG("DoReadSessionInfo", "Entry: msf=%d, allocLen=%d", msf, allocationLength);

    uint8_t sessionTOC[12] = {
        0x00, 0x0A, 0x01, 0x01, 0x00, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00};

    cueParser.restart();
    const CUETrackInfo *trackinfo = cueParser.next_track();
    if (trackinfo)
    {
        CDROM_DEBUG_LOG("DoReadSessionInfo", "First track: num=%d, start=%u",
                        trackinfo->track_number, trackinfo->data_start);

        if (msf)
        {
            sessionTOC[8] = 0;
            LBA2MSF(trackinfo->data_start, &sessionTOC[9], false);
            CDROM_DEBUG_LOG("DoReadSessionInfo", "MSF: %02x:%02x:%02x",
                            sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
        else
        {
            sessionTOC[8] = (trackinfo->data_start >> 24) & 0xFF;
            sessionTOC[9] = (trackinfo->data_start >> 16) & 0xFF;
            sessionTOC[10] = (trackinfo->data_start >> 8) & 0xFF;
            sessionTOC[11] = (trackinfo->data_start >> 0) & 0xFF;
            CDROM_DEBUG_LOG("DoReadSessionInfo", "LBA bytes: %02x %02x %02x %02x",
                            sessionTOC[8], sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
    }

    int len = sizeof(sessionTOC);
    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG("DoReadSessionInfo", "Sending %d bytes", len);
    memcpy(m_InBuffer, sessionTOC, len);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD)
{
    CDROM_DEBUG_LOG("DoReadFullTOC", "Entry: session=%d, allocLen=%d, BCD=%d",
                    session, allocationLength, useBCD);

    if (session > 1)
    {
        CDROM_DEBUG_LOG("DoReadFullTOC", "INVALID SESSION %d", session);
        setSenseData(0x05, 0x24, 0x00);
        sendCheckCondition();
        return;
    }

    // Base full TOC structure with A0/A1/A2 descriptors
    uint8_t fullTOCBase[37] = {
        0x00, 0x2E, 0x01, 0x01,
        0x01, 0x14, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t len = sizeof(fullTOCBase);
    memcpy(m_InBuffer, fullTOCBase, len);

    // Find first and last tracks
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};
    const CUETrackInfo *trackinfo;

    cueParser.restart();
    while ((trackinfo = cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
        {
            firsttrack = trackinfo->track_number;
            if (trackinfo->track_mode == CUETrack_AUDIO)
            {
                m_InBuffer[5] = 0x10; // A0 control for audio
            }
            CDROM_DEBUG_LOG("DoReadFullTOC", "First track: %d, mode=%d", firsttrack, trackinfo->track_mode);
        }
        lasttrack = *trackinfo;

        // Add track descriptor
        FormatRawTOCEntry(trackinfo, &m_InBuffer[len], useBCD);

        CDROM_DEBUG_LOG("DoReadFullTOC", "  Track %d: mode=%d, start=%u",
                        trackinfo->track_number, trackinfo->track_mode, trackinfo->data_start);

        len += 11;
    }

    // Update A0, A1, A2 descriptors
    m_InBuffer[12] = firsttrack;
    m_InBuffer[23] = lasttrack.track_number;

    CDROM_DEBUG_LOG("DoReadFullTOC", "A0: First=%d, A1: Last=%d", firsttrack, lasttrack.track_number);

    if (lasttrack.track_mode == CUETrack_AUDIO)
    {
        m_InBuffer[16] = 0x10; // A1 control
        m_InBuffer[27] = 0x10; // A2 control
    }

    // A2: Leadout position
    u32 leadoutLBA = GetLeadoutLBA();
    CDROM_DEBUG_LOG("DoReadFullTOC", "A2: Lead-out LBA=%u", leadoutLBA);

    if (useBCD)
    {
        LBA2MSFBCD(leadoutLBA, &m_InBuffer[34], false);
        CDROM_DEBUG_LOG("DoReadFullTOC", "A2 MSF (BCD): %02x:%02x:%02x",
                        m_InBuffer[34], m_InBuffer[35], m_InBuffer[36]);
    }
    else
    {
        LBA2MSF(leadoutLBA, &m_InBuffer[34], false);
        CDROM_DEBUG_LOG("DoReadFullTOC", "A2 MSF: %02x:%02x:%02x",
                        m_InBuffer[34], m_InBuffer[35], m_InBuffer[36]);
    }

    // Update TOC length
    uint16_t toclen = len - 2;
    m_InBuffer[0] = (toclen >> 8) & 0xFF;
    m_InBuffer[1] = toclen & 0xFF;

    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG("DoReadFullTOC", "Response: %d bytes (%d total, %d requested)",
                    len, toclen + 2, allocationLength);

    // LOG RESPONSE BUFFER
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG("DoReadFullTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, m_InBuffer[i + 0], m_InBuffer[i + 1], m_InBuffer[i + 2], m_InBuffer[i + 3],
                            m_InBuffer[i + 4], m_InBuffer[i + 5], m_InBuffer[i + 6], m_InBuffer[i + 7],
                            m_InBuffer[i + 8], m_InBuffer[i + 9], m_InBuffer[i + 10], m_InBuffer[i + 11],
                            m_InBuffer[i + 12], m_InBuffer[i + 13], m_InBuffer[i + 14], m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG("DoReadFullTOC", "%s", buf);
        }
    }

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

// Add these helper functions to your class
void CUSBCDGadget::LBA2MSF(int32_t LBA, uint8_t *MSF, bool relative)
{
    if (!relative)
    {
        LBA += 150; // Add 2-second pregap for absolute addressing
    }

    uint32_t ulba = LBA;
    if (LBA < 0)
    {
        ulba = LBA * -1;
    }

    MSF[2] = ulba % 75; // Frames
    uint32_t rem = ulba / 75;

    MSF[1] = rem % 60; // Seconds
    MSF[0] = rem / 60; // Minutes
}

void CUSBCDGadget::LBA2MSFBCD(int32_t LBA, uint8_t *MSF, bool relative)
{
    LBA2MSF(LBA, MSF, relative);
    MSF[0] = ((MSF[0] / 10) << 4) | (MSF[0] % 10);
    MSF[1] = ((MSF[1] / 10) << 4) | (MSF[1] % 10);
    MSF[2] = ((MSF[2] / 10) << 4) | (MSF[2] % 10);
}

int32_t CUSBCDGadget::MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative)
{
    int32_t lba = (m * 60 + s) * 75 + f;
    if (!relative)
        lba -= 150;
    return lba;
}

// Update your existing function to use the helper
u32 CUSBCDGadget::GetAddress(u32 lba, int msf, boolean relative)
{
    if (msf)
    {
        uint8_t msfBytes[3];
        LBA2MSF(lba, msfBytes, relative);
        // Return as big-endian: frames|seconds|minutes|reserved
        return (msfBytes[2] << 24) | (msfBytes[1] << 16) | (msfBytes[0] << 8) | 0x00;
    }
    return htonl(lba);
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

    // Some corrupted cd images might have a cue that references track that are
    // outside the bin.
    if (deviceSize < file_offset)
    {
        CDROM_DEBUG_LOG("CUSBCDGadget::GetLeadoutLBA",
                        "device size %llu < file_offset %lu, returning track_start %lu",
                        deviceSize, (unsigned long)file_offset, (unsigned long)track_start);
        return track_start;
    }

    // Guard against invalid sector length
    if (sector_length == 0)
    {
        MLOGERR("CUSBCDGadget::GetLeadoutLBA",
                "sector_length is 0, returning track_start %lu", (unsigned long)track_start);
        return track_start;
    }

    // We know the start position of the last track, and we know its sector length
    // and we know the file size, so we can work out the LBA of the end of the last track
    // We can't just divide the file size by sector size because sectors lengths might
    // not be consistent (e.g. multi-mode cd where track 1 is 2048
    u64 remainingBytes = deviceSize - file_offset;
    u64 lastTrackBlocks = remainingBytes / sector_length;

    // Ensure the result fits in u32 before casting
    if (lastTrackBlocks > 0xFFFFFFFF)
    {
        MLOGERR("CUSBCDGadget::GetLeadoutLBA",
                "lastTrackBlocks overflow: %llu, capping to max u32", lastTrackBlocks);
        lastTrackBlocks = 0xFFFFFFFF;
    }

    u32 ret = track_start + (u32)lastTrackBlocks; // Cast back to u32 for LBA (max ~2TB disc)

    CDROM_DEBUG_LOG("CUSBCDGadget::GetLeadoutLBA",
                    "device size is %llu, last track file offset is %lu, last track sector_length is %lu, "
                    "last track track_start is %lu, lastTrackBlocks = %llu, returning = %lu",
                    deviceSize, (unsigned long)file_offset, (unsigned long)sector_length,
                    (unsigned long)track_start, lastTrackBlocks, (unsigned long)ret);

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

void CUSBCDGadget::DoReadHeader(bool MSF, uint32_t lba, uint16_t allocationLength)
{
    // Terminate audio playback if active (MMC Annex C requirement)
    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Pause();
    }

    uint8_t mode = 1; // Default to Mode 1

    cueParser.restart();
    CUETrackInfo trackinfo = GetTrackInfoForLBA(lba);

    if (trackinfo.track_number != -1 && trackinfo.track_mode == CUETrack_AUDIO)
    {
        mode = 0; // Audio track
    }

    m_InBuffer[0] = mode;
    m_InBuffer[1] = 0; // Reserved
    m_InBuffer[2] = 0; // Reserved
    m_InBuffer[3] = 0; // Reserved

    // Track start address
    if (MSF)
    {
        m_InBuffer[4] = 0;
        LBA2MSF(lba, &m_InBuffer[5], false);
    }
    else
    {
        m_InBuffer[4] = (lba >> 24) & 0xFF;
        m_InBuffer[5] = (lba >> 16) & 0xFF;
        m_InBuffer[6] = (lba >> 8) & 0xFF;
        m_InBuffer[7] = (lba >> 0) & 0xFF;
    }

    uint8_t len = 8;
    if (len > allocationLength)
        len = allocationLength;

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadTrackInformation(u8 addressType, u32 address, u16 allocationLength)
{
    TUSBCDTrackInformationBlock response;
    memset(&response, 0, sizeof(response));

    CUETrackInfo trackInfo = {0};
    trackInfo.track_number = -1;

    // Find the track based on address type
    if (addressType == 0x00)
    {
        // LBA address
        trackInfo = GetTrackInfoForLBA(address);
    }
    else if (addressType == 0x01)
    {
        // Logical track number
        trackInfo = GetTrackInfoForTrack(address);
    }
    else if (addressType == 0x02)
    {
        // Session number - we only support session 1
        if (address == 1)
        {
            cueParser.restart();
            const CUETrackInfo *first = cueParser.next_track();
            if (first)
                trackInfo = *first;
        }
    }

    if (trackInfo.track_number == -1)
    {
        setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        sendCheckCondition();
        return;
    }

    // Calculate track length
    u32 trackLength = 0;
    cueParser.restart();
    const CUETrackInfo *nextTrack = nullptr;
    const CUETrackInfo *currentTrack = nullptr;

    while ((currentTrack = cueParser.next_track()) != nullptr)
    {
        if (currentTrack->track_number == trackInfo.track_number)
        {
            nextTrack = cueParser.next_track();
            if (nextTrack)
            {
                trackLength = nextTrack->data_start - currentTrack->data_start;
            }
            else
            {
                // Last track - calculate from file size
                u32 leadoutLBA = GetLeadoutLBA();
                trackLength = leadoutLBA - currentTrack->data_start;
            }
            break;
        }
    }

    // Fill response
    response.dataLength = htons(0x002E); // 46 bytes
    response.logicalTrackNumberLSB = trackInfo.track_number;
    response.sessionNumberLSB = 0x01;

    // Track mode
    if (trackInfo.track_mode == CUETrack_AUDIO)
    {
        response.trackMode = 0x00; // Audio, 2 channels
        response.dataMode = 0x00;
    }
    else
    {
        response.trackMode = 0x04; // Data track, uninterrupted
        response.dataMode = 0x01;  // Mode 1
    }

    // Track start and size
    response.logicalTrackStartAddress = htonl(trackInfo.data_start);
    response.logicalTrackSize = htonl(trackLength);

    // Additional fields
    response.freeBlocks = htonl(0); // No free blocks (read-only disc)

    int length = sizeof(response);
    if (allocationLength < length)
        length = allocationLength;

    memcpy(m_InBuffer, &response, length);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
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
    MLOGNOTE("CD OnActivate",
             "=== ENTRY === state=%d, USB=%s, m_CDReady=%d, mediaState=%d",
             (int)m_nState,
             m_IsFullSpeed ? "Full-Speed (USB 1.1)" : "High-Speed (USB 2.0)",
             m_CDReady, (int)m_mediaState);

    CTimer::Get()->MsDelay(10);

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

        MLOGNOTE("CD OnActivate",
                 "Initial media ready: Set UNIT_ATTENTION, sense=06/28/00");
    }

    m_nState = TCDState::ReceiveCBW;
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);

    MLOGNOTE("CD OnActivate",
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

void FillModePage2A(ModePage0x2AData &codepage)
{
    memset(&codepage, 0, sizeof(codepage));
    codepage.pageCodeAndPS = 0x2a;
    codepage.pageLength = 0x0E; // Should be 22 bytes for full MMC-5 compliance

    // Capability bits (6 bytes) - dynamic based on media type
    // Byte 0: bit0=DVD-ROM, bit1=DVD-R, bit2=DVD-RAM, bit3=CD-R, bit4=CD-RW, bit5=Method2
    codepage.capabilityBits[0] = 0x00; // Support all media types for DVD, else CD only
    codepage.capabilityBits[1] = 0x00; // All writable types
    codepage.capabilityBits[2] = 0x01; // AudioPlay, composite audio/video, digital port 2, Mode 2 Form 2, Mode 2 Form 1
    codepage.capabilityBits[3] = 0x03; // CD-DA Commands Supported, CD-DA Stream is accurate
    codepage.capabilityBits[4] = 0x28; // Tray loading mechanism, eject supported, lock supported
    codepage.capabilityBits[5] = 0x03; // No separate channel volume, no separate channel mute

    // Speed and buffer info
    codepage.maxSpeed = htons(1378);          // 8x
    codepage.numVolumeLevels = htons(0x0100); // 256 volume levels
    codepage.bufferSize = htons(0x0040);      // Set to 64 KB buffer size
    codepage.currentSpeed = htons(1378);      // Current speed
    codepage.maxReadSpeed = htons(1378);      // Some hosts check this field
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
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                 "TEST UNIT READY: m_CDReady=%d, mediaState=%d, sense=%02x/%02x/%02x",
                 m_CDReady, (int)m_mediaState,
                 m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);

        if (!m_CDReady)
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
            setSenseData(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT
            m_mediaState = MediaState::NO_MEDIUM;
            sendCheckCondition();
            break;
        }

        if (m_mediaState == MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                     "TEST UNIT READY -> CHECK CONDITION (sense 06/28/00 - UNIT ATTENTION)");
            setSenseData(0x06, 0x28, 0x00); // UNIT ATTENTION - MEDIA CHANGED
            sendCheckCondition();
            break;
        }

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                 "TEST UNIT READY -> GOOD STATUS");

        // CDROM_DEBUG_LOG ("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        sendGoodStatus();
        break;
    }

    case 0x03: // Request sense CMD
    {
        u8 blocks = (u8)(m_CBW.CBWCB[4]);

        MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                 "REQUEST SENSE: mediaState=%d, sense=%02x/%02x/%02x -> reporting to host",
                 (int)m_mediaState,
                 m_SenseParams.bSenseKey, m_SenseParams.bAddlSenseCode, m_SenseParams.bAddlSenseCodeQual);

        u8 length = sizeof(TUSBCDRequestSenseReply);
        if (blocks < length)
            length = blocks;

        // Populate sense reply with CURRENT sense data
        m_ReqSenseReply.bSenseKey = m_SenseParams.bSenseKey;
        m_ReqSenseReply.bAddlSenseCode = m_SenseParams.bAddlSenseCode;
        m_ReqSenseReply.bAddlSenseCodeQual = m_SenseParams.bAddlSenseCodeQual;

        memcpy(&m_InBuffer, &m_ReqSenseReply, length);

        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                   m_InBuffer, length);

        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK; // Request Sense always succeeds
        m_nState = TCDState::SendReqSenseReply;

        // CRITICAL FIX: Clear sense data AFTER reporting it (SCSI autoclearing behavior)
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                 "REQUEST SENSE: Clearing sense data after reporting");
        clearSenseData();

        // Update media state machine: transition from UNIT_ATTENTION to READY
        if (m_mediaState == MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
        {
            m_mediaState = MediaState::MEDIUM_PRESENT_READY;
            bmCSWStatus = CD_CSW_STATUS_OK; // Clear global CHECK CONDITION flag
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                     "REQUEST SENSE: State transition UNIT_ATTENTION -> READY");
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
            setSenseData(0x02, 0x04, 0x00); // Not Ready, Logical Unit Not Ready
            sendCheckCondition();
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
                m_nnumber_blocks = 0;           // nothing more after this send
                setSenseData(0x05, 0x24, 0x00); // Invalid Field in CDB
                sendCheckCondition();
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
        sendGoodStatus();
        break;
    }

    case 0x1E: // PREVENT ALLOW MEDIUM REMOVAL
    {
        // Lie to the host
        // m_CSW.bmCSWStatus = bmCSWStatus;
        sendGoodStatus();
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
            CDROM_DEBUG_LOG("READ(10)", "LBA=%u, cnt=%u", m_nblock_address, m_nnumber_blocks);

            m_CSW.bmCSWStatus = bmCSWStatus;
            m_nState = TCDState::DataInRead; // see Update() function
        }
        else
        {
            CDROM_DEBUG_LOG("handleSCSI Read(10)", "failed, %s", m_CDReady ? "ready" : "not ready");
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
        }
        break;
    }

    case 0xBE: // READ CD -- bluescsi inspired
    {
        if (!m_CDReady)
        {
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
            break;
        }

        int expectedSectorType = (m_CBW.CBWCB[1] >> 2) & 0x07;
        m_nblock_address = (m_CBW.CBWCB[2] << 24) | (m_CBW.CBWCB[3] << 16) |
                           (m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
        m_nnumber_blocks = (m_CBW.CBWCB[6] << 16) | (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
        mcs = (m_CBW.CBWCB[9] >> 3) & 0x1F;

        // Get track info for validation
        CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);

        // Verify sector type if specified
        if (expectedSectorType != 0)
        {
            bool sector_type_ok = false;

            if (expectedSectorType == 1 && trackInfo.track_mode == CUETrack_AUDIO)
            {
                sector_type_ok = true; // CD-DA
            }
            else if (expectedSectorType == 2 &&
                     (trackInfo.track_mode == CUETrack_MODE1_2048 ||
                      trackInfo.track_mode == CUETrack_MODE1_2352))
            {
                sector_type_ok = true; // Mode 1
            }
            else if (expectedSectorType == 3 && trackInfo.track_mode == CUETrack_MODE2_2352)
            {
                sector_type_ok = true; // Mode 2 formless
            }
            else if (expectedSectorType == 4 && trackInfo.track_mode == CUETrack_MODE2_2352)
            {
                sector_type_ok = true; // Mode 2 form 1
            }
            else if (expectedSectorType == 5 && trackInfo.track_mode == CUETrack_MODE2_2352)
            {
                sector_type_ok = true; // Mode 2 form 2
            }

            if (!sector_type_ok)
            {
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                                "READ CD: Sector type mismatch. Expected=%d, Track mode=%d",
                                expectedSectorType, trackInfo.track_mode);
                setSenseData(0x05, 0x64, 0x00); // ILLEGAL MODE FOR THIS TRACK
                sendCheckCondition();
                break;
            }
        }

        // Ensure read doesn't exceed image size
        u64 readEnd = (u64)m_nblock_address * trackInfo.sector_length +
                      (u64)m_nnumber_blocks * trackInfo.sector_length;
        if (readEnd > m_pDevice->GetSize())
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand",
                     "READ CD: Read exceeds image size");
            setSenseData(0x05, 0x21, 0x00); // LOGICAL BLOCK ADDRESS OUT OF RANGE
            sendCheckCondition();
            break;
        }

        // Determine sector parameters based on expected type or track mode
        switch (expectedSectorType)
        {
        case 0x01: // CD-DA
            block_size = 2352;
            transfer_block_size = 2352;
            skip_bytes = 0;
            break;

        case 0x02: // Mode 1
            skip_bytes = GetSkipbytesForTrack(trackInfo);
            block_size = GetBlocksizeForTrack(trackInfo);
            transfer_block_size = 2048;
            break;

        case 0x03: // Mode 2 formless
            skip_bytes = 16;
            block_size = 2352;
            transfer_block_size = 2336;
            break;

        case 0x04: // Mode 2 form 1
            skip_bytes = GetSkipbytesForTrack(trackInfo);
            block_size = GetBlocksizeForTrack(trackInfo);
            transfer_block_size = 2048;
            break;

        case 0x05: // Mode 2 form 2
            block_size = 2352;
            skip_bytes = 24;
            transfer_block_size = 2328;
            break;

        case 0x00: // Type not specified - derive from MCS and track mode
        default:
            if (trackInfo.track_mode == CUETrack_AUDIO)
            {
                block_size = 2352;
                transfer_block_size = 2352;
                skip_bytes = 0;
            }
            else
            {
                block_size = GetBlocksizeForTrack(trackInfo);
                transfer_block_size = GetSectorLengthFromMCS(mcs);
                skip_bytes = GetSkipBytesFromMCS(mcs);
            }
            break;
        }

        // Minimal logging - command parameters only (audio detection deferred to Update())
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                        "READ CD: USB=%s, LBA=%u, blocks=%u, type=0x%02x, MCS=0x%02x",
                        m_IsFullSpeed ? "FS" : "HS",
                        m_nblock_address, m_nnumber_blocks,
                        expectedSectorType, mcs);

        m_nbyteCount = m_CBW.dCBWDataTransferLength;
        if (m_nnumber_blocks == 0)
        {
            m_nnumber_blocks = 1 + (m_nbyteCount) / transfer_block_size;
        }

        m_nState = TCDState::DataInRead;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
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

    case 0x43: // READ TOC/PMA/ATIP -- bluescsi inspired
    {
        if (!m_CDReady)
        {
            MLOGNOTE("READ TOC", "FAILED - CD not ready");
            setSenseData(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY
            sendCheckCondition();
            break;
        }

        // LOG FULL COMMAND BYTES
        CDROM_DEBUG_LOG("READ TOC", "CMD bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                        m_CBW.CBWCB[0], m_CBW.CBWCB[1], m_CBW.CBWCB[2], m_CBW.CBWCB[3],
                        m_CBW.CBWCB[4], m_CBW.CBWCB[5], m_CBW.CBWCB[6], m_CBW.CBWCB[7],
                        m_CBW.CBWCB[8], m_CBW.CBWCB[9]);

        bool msf = (m_CBW.CBWCB[1] >> 1) & 0x01;
        int format = m_CBW.CBWCB[2] & 0x0F;
        int startingTrack = m_CBW.CBWCB[6];
        int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

        // Check for vendor extension flags (Matshita compatibility)
        bool useBCD = false;
        if (format == 0 && m_CBW.CBWCB[9] == 0x80)
        {
            format = 2;
            useBCD = true;
            CDROM_DEBUG_LOG("READ TOC", "Matshita vendor extension: Full TOC with BCD");
        }

        CDROM_DEBUG_LOG("READ TOC", "Format=%d MSF=%d StartTrack=%d AllocLen=%d Control=0x%02x",
                        format, msf, startingTrack, allocationLength, m_CBW.CBWCB[9]);

        if (!m_CDReady)
        {
            MLOGNOTE("READ TOC", "FAILED - CD not ready");
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
            break;
        }

        switch (format)
        {
        case 0:
            CDROM_DEBUG_LOG("READ TOC", "Format 0x00: Standard TOC");
            DoReadTOC(msf, startingTrack, allocationLength);
            break;
        case 1:
            CDROM_DEBUG_LOG("READ TOC", "Format 0x01: Session Info");
            DoReadSessionInfo(msf, allocationLength);
            break;
        case 2:
            CDROM_DEBUG_LOG("READ TOC", "Format 0x02: Full TOC (useBCD=%d)", useBCD);
            DoReadFullTOC(startingTrack, allocationLength, useBCD);
            break;
        default:
            CDROM_DEBUG_LOG("READ TOC", "INVALID FORMAT 0x%02x", format);
            setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
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
            header.audioStatus = 0x15; // Audio status not supported
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
                data.absoluteAddress = GetAddress(address, msf, false);
                CUETrackInfo trackInfo = GetTrackInfoForLBA(address);
                if (trackInfo.track_number != -1)
                {
                    data.trackNumber = trackInfo.track_number;
                    data.indexNumber = 0x01; // Assume no pregap. Perhaps we need to handle pregap?
                    data.relativeAddress = GetAddress(address - trackInfo.track_start, msf, true);
                    // Set ADR/Control: ADR=1 (position), Control=0 for audio, 4 for data
                    u8 control = (trackInfo.track_mode == CUETrack_AUDIO) ? 0x00 : 0x04;
                    data.adrControl = (0x01 << 4) | control;
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

    case 0x52: // READ TRACK INFORMATION -- bluescsi inspired
    {
        u8 addressType = m_CBW.CBWCB[1] & 0x03;
        u32 address = (m_CBW.CBWCB[2] << 24) | (m_CBW.CBWCB[3] << 16) |
                      (m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
        u16 allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                        "Read Track Information type=%d, addr=%u", addressType, address);

        if (!m_CDReady)
        {
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
            break;
        }

        DoReadTrackInformation(addressType, address, allocationLength);
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

    case 0xAD: // READ DISC STRUCTURE (formerly READ DVD STRUCTURE)
    {
        u8 mediaType = m_CBW.CBWCB[1] & 0x0f; // Media type (0=DVD, 1=BD)
        u32 address = ((u32)m_CBW.CBWCB[2] << 24) | ((u32)m_CBW.CBWCB[3] << 16) |
                      ((u32)m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
        u8 layer = m_CBW.CBWCB[6];
        u8 format = m_CBW.CBWCB[7];
        u16 allocationLength = ((u16)m_CBW.CBWCB[8] << 8) | m_CBW.CBWCB[9];
        u8 agid = (m_CBW.CBWCB[10] >> 6) & 0x03; // Authentication Grant ID

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                        "READ DISC STRUCTURE: media=%d, format=0x%02x, layer=%d, address=0x%08x, alloc=%d, AGID=%d, mediaType=%d",
                        mediaType, format, layer, address, allocationLength, agid, (int)m_mediaType);

        // For CD media and DVD-specific formats: return minimal empty response
        // MacOS doesn't handle CHECK CONDITION well for this command - causes USB reset
        if (m_mediaType != MEDIA_TYPE::DVD &&
            (format == 0x00 || format == 0x02 || format == 0x03 || format == 0x04))
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE format 0x%02x for CD media - returning minimal response", format);

            // Return minimal header indicating no data available
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(header));
            header.dataLength = htons(2); // Just header, no payload

            int length = sizeof(header);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &header, length);
            m_nnumber_blocks = 0;
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }

        // Process DVD structures
        int dataLength = 0;

        switch (format)
        {
        case 0x00: // Physical Format Information
        {
            if (m_mediaType != MEDIA_TYPE::DVD)
            {
                // Not supported for CD
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                                "READ DISC STRUCTURE format 0x00 not supported for CD media");
                setSenseData(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
                sendCheckCondition();
                break;
            }

            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE format 0x00: Physical Format Information");

            // Build response: Header + Physical Format Info
            TUSBCDReadDiscStructureHeader header;
            DVDPhysicalFormatInfo physInfo;

            memset(&header, 0, sizeof(header));
            memset(&physInfo, 0, sizeof(physInfo));

            // Calculate disc capacity (you may want to get this from your CUE parser)
            u32 discCapacity = 2298496; // Default: ~4.7GB single-layer DVD-ROM (2,298,496 sectors)

            // Byte 0: Book type and part version
            // Book type: 0x00 = DVD-ROM, 0x0A = DVD+R
            // Part version: 0x01 for DVD-ROM
            physInfo.bookTypePartVer = 0x01; // DVD-ROM, version 1.0

            // Byte 1: Disc size and maximum rate
            // Disc size: 0x00 = 120mm, 0x01 = 80mm
            // Max rate: 0x02 = 10.08 Mbps
            physInfo.discSizeMaxRate = 0x20; // Max rate=2, disc size=0

            // Byte 2: Layers, path, type
            // Num layers: 0x00 = 1 layer, 0x01 = 2 layers
            // Track path: 0 = Parallel
            // Layer type: 0x01 = embossed data layer (bit 0)
            physInfo.layersPathType = 0x01; // Single layer, parallel, embossed

            // Byte 3: Densities
            // Linear density: 0x00 = 0.267 um/bit
            // Track density: 0x00 = 0.74 um/track
            physInfo.densities = 0x00;

            // Bytes 4-6: Data start sector (24-bit big-endian)
            // Standard DVD starts at 0x030000
            u32 dataStart = 0x030000;
            physInfo.dataStartSector[0] = (dataStart >> 16) & 0xFF;
            physInfo.dataStartSector[1] = (dataStart >> 8) & 0xFF;
            physInfo.dataStartSector[2] = dataStart & 0xFF;

            // Bytes 7-9: Data end sector (24-bit big-endian)
            u32 dataEnd = dataStart + discCapacity;
            physInfo.dataEndSector[0] = (dataEnd >> 16) & 0xFF;
            physInfo.dataEndSector[1] = (dataEnd >> 8) & 0xFF;
            physInfo.dataEndSector[2] = dataEnd & 0xFF;

            // Bytes 10-12: Layer 0 end (24-bit big-endian)
            // For single layer, this is 0
            physInfo.layer0EndSector[0] = 0x00;
            physInfo.layer0EndSector[1] = 0x00;
            physInfo.layer0EndSector[2] = 0x00;

            // Byte 13: BCA flag
            physInfo.bcaFlag = 0x00; // No BCA

            // Bytes 14-16: Reserved
            physInfo.reserved[0] = 0x00;
            physInfo.reserved[1] = 0x00;
            physInfo.reserved[2] = 0x00;

            // Set header length (excludes the header itself)
            header.dataLength = htons(sizeof(physInfo));

            // Copy to buffer
            memcpy(m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);
            memcpy(m_InBuffer + dataLength, &physInfo, sizeof(physInfo));
            dataLength += sizeof(physInfo);

            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "DVD Physical Format: dataStart=0x%06x, dataEnd=0x%06x, totalLength=%d",
                            dataStart, dataEnd, dataLength);
            break;
        }

        case 0x01: // Copyright Information
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE format 0x01: Copyright Information (CSS=%d)",
                            m_bReportDVDCSS);

            // Build response: Header + Copyright Info
            TUSBCDReadDiscStructureHeader header;
            DVDCopyrightInfo copyInfo;

            memset(&header, 0, sizeof(header));
            memset(&copyInfo, 0, sizeof(copyInfo));

            // Set copyright protection type
            if (m_bReportDVDCSS && m_mediaType == MEDIA_TYPE::DVD)
            {
                copyInfo.copyrightProtectionType = 0x01; // CSS/CPPM
                copyInfo.regionManagementInfo = 0x00;    // All regions (0xFF = no regions)
            }
            else
            {
                copyInfo.copyrightProtectionType = 0x00; // No protection
                copyInfo.regionManagementInfo = 0x00;    // N/A
            }

            copyInfo.reserved1 = 0x00;
            copyInfo.reserved2 = 0x00;

            // Set header length
            header.dataLength = htons(sizeof(copyInfo));

            // Copy to buffer
            memcpy(m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);
            memcpy(m_InBuffer + dataLength, &copyInfo, sizeof(copyInfo));
            dataLength += sizeof(copyInfo);
            break;
        }

        case 0x04: // Manufacturing Information
        {
            if (m_mediaType != MEDIA_TYPE::DVD)
            {
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                                "READ DISC STRUCTURE format 0x04 not supported for CD media");
                setSenseData(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
                sendCheckCondition();
                break;
            }

            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE format 0x04: Manufacturing Information");

            // Return 2048 bytes of zeroed manufacturing data
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(header));
            header.dataLength = htons(2048);

            memcpy(m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);

            // Add 2048 bytes of zeros
            memset(m_InBuffer + dataLength, 0, 2048);
            dataLength += 2048;
            break;
        }

        case 0xFF: // Format List
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE format 0xFF: Disc Structure List");

            // Build list of supported formats
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(header));

            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                // DVD supports: 0x00 (Physical), 0x01 (Copyright), 0x04 (Manufacturing), 0xFF (List)
                u8 formatList[] = {
                    0x00, 0x00, 0x00, 0x00, // Format 0x00: Physical Format
                    0x01, 0x00, 0x00, 0x00, // Format 0x01: Copyright
                    0x04, 0x00, 0x00, 0x00, // Format 0x04: Manufacturing
                    0xFF, 0x00, 0x00, 0x00  // Format 0xFF: List
                };

                header.dataLength = htons(sizeof(formatList));
                memcpy(m_InBuffer, &header, sizeof(header));
                dataLength += sizeof(header);
                memcpy(m_InBuffer + dataLength, formatList, sizeof(formatList));
                dataLength += sizeof(formatList);
            }
            else
            {
                // CD only supports: 0x01 (Copyright), 0xFF (List)
                u8 formatList[] = {
                    0x01, 0x00, 0x00, 0x00, // Format 0x01: Copyright
                    0xFF, 0x00, 0x00, 0x00  // Format 0xFF: List
                };

                header.dataLength = htons(sizeof(formatList));
                memcpy(m_InBuffer, &header, sizeof(header));
                dataLength += sizeof(header);
                memcpy(m_InBuffer + dataLength, formatList, sizeof(formatList));
                dataLength += sizeof(formatList);
            }
            break;
        }

        default: // Unsupported format
        {
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                            "READ DISC STRUCTURE: Unsupported format 0x%02x", format);

            // Return minimal empty structure
            TUSBCDReadDiscStructureHeader header;
            memset(&header, 0, sizeof(header));
            header.dataLength = htons(0); // No data

            memcpy(m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);
            break;
        }
        }

        // Truncate to allocation length
        if (allocationLength < dataLength)
            dataLength = allocationLength;

        // Send response
        m_nnumber_blocks = 0;
        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, dataLength);
        m_nState = TCDState::DataIn;
        m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
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

    case 0x44: // READ HEADER
    {
        bool MSF = (m_CBW.CBWCB[1] & 0x02);
        uint32_t lba = (m_CBW.CBWCB[2] << 24) | (m_CBW.CBWCB[3] << 16) |
                       (m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
        uint16_t allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                        "Read Header lba=%u, MSF=%d", lba, MSF);

        if (!m_CDReady)
        {
            setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
            sendCheckCondition();
            break;
        }

        DoReadHeader(MSF, lba, allocationLength);
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

            memcpy(m_InBuffer + dataLength, &randomreadable, sizeof(randomreadable));
            dataLength += sizeof(randomreadable);
            // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Sending Random Readable feature (0x0010)", rt);

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

            if (m_mediaType == MEDIA_TYPE::DVD)
            {
                memcpy(m_InBuffer + dataLength, &dvdcss, sizeof(dvdcss));
                dataLength += sizeof(dvdcss);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x%02x): Sending DVD CSS feature (0x0106)", rt);
            }

            memcpy(m_InBuffer + dataLength, &audioplay, sizeof(audioplay));
            dataLength += sizeof(audioplay);

            memcpy(m_InBuffer + dataLength, &rtstreaming, sizeof(rtstreaming));
            dataLength += sizeof(rtstreaming);

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

            case 0x10:
            { // Random Readable - CRITICAL for CD-ROM operation
                memcpy(m_InBuffer + dataLength, &randomreadable, sizeof(randomreadable));
                dataLength += sizeof(randomreadable);
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02, feat 0x10): Sending Random Readable");
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

            case 0x106:
            { // DVD CSS - Only return for DVD media
                if (m_mediaType == MEDIA_TYPE::DVD)
                {
                    memcpy(m_InBuffer + dataLength, &dvdcss, sizeof(dvdcss));
                    dataLength += sizeof(dvdcss);
                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02, feat 0x106): Sending DVD CSS");
                }
                break;
            }

            case 0x107:
            { // Real Time Streaming - CRITICAL for CD-DA playback
                memcpy(m_InBuffer + dataLength, &rtstreaming, sizeof(rtstreaming));
                dataLength += sizeof(rtstreaming);
                // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "GET CONFIGURATION (rt 0x02, feat 0x107): Sending Real Time Streaming");
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

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "SEEK to LBA %lu", m_nblock_address);

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
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO MSF. Start MSF %d:%d:%d, End MSF: %d:%d:%d, start LBA %u, end LBA %u", SM, SS, SF, EM, ES, EF, start_lba, end_lba);

        CUETrackInfo trackInfo = GetTrackInfoForLBA(start_lba);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            // Play the audio
            CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "CD Player found, sending command");
            CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer)
            {
                if (start_lba == 0xFFFFFFFF)
                {
                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "CD Player found, Resume");
                    cdplayer->Resume();
                }
                else if (start_lba == end_lba)
                {
                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "CD Player found, Pause");
                    cdplayer->Pause();
                }
                else
                {
                    CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "CD Player found, Play");
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

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand",
                        "PLAY AUDIO (10): LBA=%u, blocks=%u, USB=%s",
                        m_nblock_address, m_nnumber_blocks,
                        m_IsFullSpeed ? "FS" : "HS");

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

        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (12) Playing from %lu for %lu blocks", m_nblock_address, m_nnumber_blocks);

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
                if (page != 0x3f)
                        break;            
                }            

            case 0x08:
            {
                // Mode Page 0x08 (Caching)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x08 (Caching)");

                ModePage0x08Data codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x08;
                codepage.pageLength = 0x12;
                codepage.cachingFlags = 0x00;  // RCD=0, WCE=0

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
                codepage.IMMEDAndSOTC = 0x05;
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

            case 0x1c:
            {
                // Mode Page 0x1C (Informational Exceptions Control)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x1c response");

                ModePage0x1CData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x1c;
                codepage.pageLength = 0x0a;
                codepage.flags = 0x00;  // No special flags
                codepage.mrie = 0x00;   // No reporting

                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }

            case 0x31:
            {
                // Page 0x31 - Apple vendor-specific page
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x31 (Apple vendor page)");

                // Return minimal vendor page structure
                struct VendorPage {
                    u8 pageCode;
                    u8 pageLength;
                    u8 reserved[10];  // Minimal padding
                } PACKED;

                VendorPage codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCode = 0x31;
                codepage.pageLength = 0x0a;  // 10 bytes

                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }            

            default:
            {
                // We don't support this code page
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) unsupported page 0x%02x", page);
                setSenseData(0x05, 0x24, 0x00);
                sendCheckCondition();
                return;
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
        CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) RAW CBWCB[2]=0x%02x, page=0x%02x, page_control=0x%02x", m_CBW.CBWCB[2], page, page_control);
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
            case 0x08:
            {
                // Mode Page 0x08 (Caching)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x08 (Caching)");

                ModePage0x08Data codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x08;
                codepage.pageLength = 0x12;
                codepage.cachingFlags = 0x00;  // RCD=0, WCE=0

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
                if (page != 0x3f)
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

            case 0x1c:
            {
                // Mode Page 0x1C (Informational Exceptions Control)
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) 0x1c response");

                ModePage0x1CData codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCodeAndPS = 0x1c;
                codepage.pageLength = 0x0a;
                codepage.flags = 0x00;  // No special flags
                codepage.mrie = 0x00;   // No reporting

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
                codepage.IMMEDAndSOTC = 0x05;
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

            case 0x31:
            {
                // Page 0x31 - Apple vendor-specific page
                CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (6) 0x31 (Apple vendor page)");

                // Return minimal vendor page structure
                struct VendorPage {
                    u8 pageCode;
                    u8 pageLength;
                    u8 reserved[10];  // Minimal padding
                } PACKED;

                VendorPage codepage;
                memset(&codepage, 0, sizeof(codepage));
                codepage.pageCode = 0x31;
                codepage.pageLength = 0x0a;  // 10 bytes

                memcpy(m_InBuffer + length, &codepage, sizeof(codepage));
                length += sizeof(codepage);

                if (page != 0x3f)
                    break;
            }                      

            default:
            {
                // We don't support this code page
                // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) unsupported page 0x%02x", page);
                setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN COMMAND PACKET
                sendCheckCondition();
                return;
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
            CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
            bool isAudioTrack = (trackInfo.track_number != -1 &&
                                 trackInfo.track_mode == CUETrack_AUDIO);

            // Single seek operation (no logging in hot path)
            offset = m_pDevice->Seek(block_size * m_nblock_address);

            if (offset != (u64)(-1))
            {
                // Get speed-appropriate limits
                size_t maxBlocks = m_IsFullSpeed ? MaxBlocksToReadFullSpeed : MaxBlocksToReadHighSpeed;
                size_t maxBufferSize = m_IsFullSpeed ? MaxInMessageSizeFullSpeed : MaxInMessageSize;

                u32 blocks_to_read_in_batch = m_nnumber_blocks;

                if (blocks_to_read_in_batch > maxBlocks)
                {
                    blocks_to_read_in_batch = maxBlocks;
                    m_nnumber_blocks -= maxBlocks;
                }
                else
                {
                    m_nnumber_blocks = 0;
                }

                // Calculate sizes
                u32 total_batch_size = blocks_to_read_in_batch * block_size;
                u32 total_transfer_size = blocks_to_read_in_batch * transfer_block_size;

                // Validate against buffer limits
                if (total_transfer_size > maxBufferSize)
                {
                    u32 safe_blocks = maxBufferSize / transfer_block_size;
                    blocks_to_read_in_batch = safe_blocks;
                    total_batch_size = blocks_to_read_in_batch * block_size;
                    total_transfer_size = blocks_to_read_in_batch * transfer_block_size;

                    if (m_nnumber_blocks > 0)
                    {
                        m_nnumber_blocks += (maxBlocks - blocks_to_read_in_batch);
                    }
                }

                // Secondary safety check
                if (total_batch_size > MaxInMessageSize)
                {
                    MLOGERR("UpdateRead", "BUFFER OVERFLOW: %u > %u",
                            total_batch_size, (u32)MaxInMessageSize);
                    blocks_to_read_in_batch = MaxInMessageSize / block_size;
                    total_batch_size = blocks_to_read_in_batch * block_size;
                    total_transfer_size = blocks_to_read_in_batch * transfer_block_size;
                    m_nnumber_blocks = 0;
                }

                // Perform read (no logging unless debug enabled)
                readCount = m_pDevice->Read(m_FileChunk, total_batch_size);

                if (readCount < static_cast<int>(total_batch_size))
                {
                    MLOGERR("UpdateRead", "Short read: %d/%u", readCount, total_batch_size);
                    setSenseData(0x04, 0x11, 0x00);
                    sendCheckCondition();
                    return;
                }

                // Optimized buffer copy
                u8 *dest_ptr = m_InBuffer;
                u32 total_copied = 0;

                if (transfer_block_size == block_size && skip_bytes == 0)
                {
                    // FAST PATH: Direct copy when no reconstruction needed
                    memcpy(dest_ptr, m_FileChunk, total_transfer_size);
                    total_copied = total_transfer_size;
                }
                else if (transfer_block_size > block_size)
                {
                    // MCS sector reconstruction (optimized)
                    for (u32 i = 0; i < blocks_to_read_in_batch; ++i)
                    {
                        u8 sector2352[2352] = {0};
                        int offset = 0;

                        // SYNC (12 bytes) - single memset instead of 3
                        if (mcs & 0x10)
                        {
                            sector2352[0] = 0x00;
                            memset(&sector2352[1], 0xFF, 10);
                            sector2352[11] = 0x00;
                            offset = 12;
                        }

                        // HEADER (4 bytes)
                        if (mcs & 0x08)
                        {
                            u32 lba = m_nblock_address + i + 150;
                            sector2352[offset++] = lba / (75 * 60); // minutes
                            sector2352[offset++] = (lba / 75) % 60; // seconds
                            sector2352[offset++] = lba % 75;        // frames
                            sector2352[offset++] = 0x01;
                        }

                        // USER DATA (2048 bytes) - single memcpy
                        if (mcs & 0x04)
                        {
                            memcpy(&sector2352[offset], m_FileChunk + (i * block_size), 2048);
                            offset += 2048;
                        }

                        // EDC/ECC (288 bytes) - already zeroed by initialization
                        if (mcs & 0x02)
                        {
                            offset += 288;
                        }

                        memcpy(dest_ptr, sector2352 + skip_bytes, transfer_block_size);
                        dest_ptr += transfer_block_size;
                        total_copied += transfer_block_size;
                    }
                }
                else
                {
                    // SKIP PATH: Simple copy with skip_bytes offset
                    for (u32 i = 0; i < blocks_to_read_in_batch; ++i)
                    {
                        memcpy(dest_ptr, m_FileChunk + (i * block_size) + skip_bytes,
                               transfer_block_size);
                        dest_ptr += transfer_block_size;
                        total_copied += transfer_block_size;
                    }
                }

                // Update state
                m_nblock_address += blocks_to_read_in_batch;
                m_nbyteCount -= total_copied;
                m_nState = TCDState::DataIn;

                uintptr_t buffer_start = (uintptr_t)m_InBuffer;
                uintptr_t buffer_end = buffer_start + total_copied;

                // Align to cache line boundaries (64 bytes on ARM)
                buffer_start &= ~63UL;                  // Round down to cache line
                buffer_end = (buffer_end + 63) & ~63UL; // Round up

                for (uintptr_t addr = buffer_start; addr < buffer_end; addr += 64)
                {
#if AARCH == 64
                    asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
#else
                    asm volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(addr) : "memory");
#endif
                }

                DataSyncBarrier();
                // SINGLE LOG: Only log completion if debug enabled
                CDROM_DEBUG_LOG("UpdateRead", "Transferred %u bytes, next_LBA=%u, remaining=%u",
                                total_copied, m_nblock_address, m_nnumber_blocks);

                // Begin USB transfer
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                           m_InBuffer, total_copied);
            }
        }

        if (!m_CDReady || offset == (u64)(-1))
        {
            MLOGERR("UpdateRead", "Failed: ready=%d, offset=%llu", m_CDReady, offset);
            setSenseData(0x02, 0x04, 0x00);
            sendCheckCondition();
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
