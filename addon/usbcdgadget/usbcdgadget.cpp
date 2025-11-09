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
#include "cdrom_util.h"
#include "scsi_command_dispatcher.h"
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

CUSBCDGadget::CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, ICueDevice *pDevice)
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
    else{
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
void CUSBCDGadget::SetDevice(ICueDevice *dev)
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

    data_skip_bytes = GetSkipbytes(this);
    data_block_size = GetBlocksize(this);

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
    ScsiCommandDispatcher::Dispatch(this, &m_CBW);
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
        CUETrackInfo trackInfo = GetTrackInfoForLBA(this, m_nblock_address);
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
                        sector2352[offset++] = lba / (75 * 60);          // minutes
                        sector2352[offset++] = (lba / 75) % 60;          // seconds
                        sector2352[offset++] = lba % 75;                 // frames
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
            buffer_start &= ~63UL;  // Round down to cache line
            buffer_end = (buffer_end + 63) & ~63UL;  // Round up

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
//NEW VERSION OF THE CODE