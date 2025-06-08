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
#include <circle/usb/gadget/usbcdgadget.h>
#include <circle/usb/gadget/usbcdgadgetendpoint.h>
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
        0x200,  // bcdUSB
        0,      // bDeviceClass
        0,      // bDeviceSubClass
        0,      // bDeviceProtocol
        64,     // bMaxPacketSize0
        // 0x04da, // Panasonic
        // 0x0d01,	// CDROM
        USB_GADGET_VENDOR_ID,
        USB_GADGET_DEVICE_ID_CD,
        0x000,    // bcdDevice
        1, 2, 0,  // strings
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
            0x80,    // bmAttributes (bus-powered)
            500 / 2  // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            //0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol
            0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol
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
            0x80,    // bmAttributes (bus-powered)
            500 / 2  // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            //0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol
            0x08, 0x06, 0x50,  // bInterfaceClass, SubClass, Protocol
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0                                                                            // bInterval
        }};

const char* const CUSBCDGadget::s_StringDescriptor[] =
    {
        "\x04\x03\x09\x04",  // Language ID
        "USBODE",
        "USB Optical Disk Emulator"};

CUSBCDGadget::CUSBCDGadget(CInterruptSystem* pInterruptSystem, boolean isFullSpeed, CCueBinFileDevice* pDevice)
    : CDWUSBGadget(pInterruptSystem,
                   isFullSpeed ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr}
{
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "entered %d", isFullSpeed);
    m_IsFullSpeed = isFullSpeed;
    if (pDevice)
        SetDevice(pDevice);
}

CUSBCDGadget::~CUSBCDGadget(void) {
    assert(0);
}

void hexdump(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    char line[80];

    for (size_t i = 0; i < size; i += 16) {
        char* ptr = line;

        // Offset
        ptr += sprintf(ptr, "%08x  ", i);

        // Hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size)
                ptr += sprintf(ptr, "%02x ", p[i + j]);
            else
                ptr += sprintf(ptr, "   ");
        }

        // Space before ASCII
        ptr += sprintf(ptr, " ");

        // ASCII representation
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                unsigned char c = p[i + j];
                ptr += sprintf(ptr, "%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }

        // Final null terminator
        *ptr = '\0';

        MLOGNOTE("hexdump", "%s", line);
    }
}

const void* CUSBCDGadget::GetDescriptor(u16 wValue, u16 wIndex, size_t* pLength) {
    MLOGNOTE("CUSBCDGadget::GetDescriptor", "entered");
    assert(pLength);

    u8 uchDescIndex = wValue & 0xFF;

    switch (wValue >> 8) {
        case DESCRIPTOR_DEVICE:
            MLOGNOTE("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_DEVICE %02x", uchDescIndex);
            if (!uchDescIndex) {
                *pLength = sizeof s_DeviceDescriptor;
                return &s_DeviceDescriptor;
            }
            break;

        case DESCRIPTOR_CONFIGURATION:
            MLOGNOTE("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_CONFIGURATION %02x", uchDescIndex);
            if (!uchDescIndex) {
                *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
		return m_IsFullSpeed?&s_ConfigurationDescriptorFullSpeed : &s_ConfigurationDescriptorHighSpeed;
            }
            break;

        case DESCRIPTOR_STRING:
            MLOGNOTE("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_STRING %02x", uchDescIndex);
            if (!uchDescIndex) {
                *pLength = (u8)s_StringDescriptor[0][0];
                return s_StringDescriptor[0];
            } else if (uchDescIndex < sizeof s_StringDescriptor / sizeof s_StringDescriptor[0]) {
                return ToStringDescriptor(s_StringDescriptor[uchDescIndex], pLength);
            }
            break;

        default:
            break;
    }

    return nullptr;
}

void CUSBCDGadget::AddEndpoints(void) {
    MLOGNOTE("CUSBCDGadget::AddEndpoints", "entered");
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

    m_nState = TCDState::Init;
}

// must set device before usb activation
void CUSBCDGadget::SetDevice(CCueBinFileDevice* dev) {
    MLOGNOTE("CUSBCDGadget::SetDevice", "entered");
    // Are we changing the device?
    if (m_pDevice && m_pDevice != dev) {
        MLOGNOTE("CUSBCDGadget::SetDevice", "Changing device");

        delete m_pDevice;
        m_pDevice = nullptr;

        // Tell the host the disc has changed
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        bSenseKey = 0x06;           // Unit Attention
        bAddlSenseCode = 0x28;      // NOT READY TO READY CHANGE
        bAddlSenseCodeQual = 0x00;  // MEDIUM MAY HAVE CHANGED
    }

    m_pDevice = dev;

    cueParser = CUEParser(m_pDevice->GetCueSheet());  // FIXME. Ensure cuesheet is not null or empty

    MLOGNOTE("CUSBCDGadget::SetDevice", "entered");

    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();

    m_CDReady = true;
    MLOGNOTE("CUSBCDGadget::SetDevice", "Block size is %d, m_CDReady = %d", block_size, m_CDReady);

    // Hand the device to the CD Player
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "Passed CueBinFileDevice to cd player");
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

    u32 deviceSize = (u32)m_pDevice->GetSize();

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
    MLOGNOTE("CUSBCDGadget::OnSuspend", "entered");
    delete m_pEP[EPOut];
    m_pEP[EPOut] = nullptr;

    delete m_pEP[EPIn];
    m_pEP[EPIn] = nullptr;

    m_nState = TCDState::Init;
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
    // MLOGNOTE("OnXferComplete", "state = %i, dir = %s, len=%i ",m_nState,bIn?"IN":"OUT",nLength);
    assert(m_nState != TCDState::Init);
    if (bIn)  // packet to host has been transferred
    {
        switch (m_nState) {
            case TCDState::SentCSW: {
                m_nState = TCDState::ReceiveCBW;
                m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut,
                                            m_OutBuffer, SIZE_CBW);
                break;
            }
            case TCDState::DataIn: {
                if (m_nnumber_blocks > 0) {
                    if (m_CDReady) {
                        m_nState = TCDState::DataInRead;  // see Update function
                    } else {
                        MLOGERR("onXferCmplt DataIn", "failed, %s",
                                m_CDReady ? "ready" : "not ready");
                        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                        bSenseKey = 0x02;
                        bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                        bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                        SendCSW();
                    }
                } else  // done sending data to host
                {
                    SendCSW();
                }
                break;
            }
            case TCDState::SendReqSenseReply: {
                SendCSW();
                break;
            }
            default: {
                MLOGERR("onXferCmplt", "dir=in, unhandled state = %i", m_nState);
                assert(0);
                break;
            }
        }
    } else  // packet from host is available in m_OutBuffer
    {
        switch (m_nState) {
            case TCDState::ReceiveCBW: {
                if (nLength != SIZE_CBW) {
                    MLOGERR("ReceiveCBW", "Invalid CBW len = %i", nLength);
                    m_pEP[EPIn]->StallRequest(true);
                    break;
                }
                memcpy(&m_CBW, m_OutBuffer, SIZE_CBW);
                if (m_CBW.dCBWSignature != VALID_CBW_SIG) {
                    MLOGERR("ReceiveCBW", "Invalid CBW sig = 0x%x",
                            m_CBW.dCBWSignature);
                    m_pEP[EPIn]->StallRequest(true);
                    break;
                }
                m_CSW.dCSWTag = m_CBW.dCBWTag;
                if (m_CBW.bCBWCBLength <= 16 && m_CBW.bCBWLUN == 0)  // meaningful CBW
                {
                    HandleSCSICommand();  // will update m_nstate
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
                assert(0);
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
                // Perhaps there's something I'm missing here?
                // Quake, for example, always sends 00 as the volume level :O
                // Tombraider works initially then starts always sending 00
                // as the volume level. I suspect this has something to do with
                // the mode sense command. Perhaps we're feeding back the wrong thing?
                // cdplayer->SetVolume(modePage->Output0Volume);
            }
            break;
        }
    }
}

// will be called before vendor request 0xfe
void CUSBCDGadget::OnActivate() {
    MLOGNOTE("CD OnActivate", "state = %i", m_nState);
    m_CDReady = true;
    m_nState = TCDState::ReceiveCBW;
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);
}

void CUSBCDGadget::SendCSW() {
    // MLOGNOTE ("CUSBCDGadget::SendCSW", "entered");
    memcpy(&m_InBuffer, &m_CSW, SIZE_CSW);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCSWIn, m_InBuffer, SIZE_CSW);
    m_nState = TCDState::SentCSW;
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
void CUSBCDGadget::HandleSCSICommand() {
    // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "SCSI Command is 0x%02x", m_CBW.CBWCB[0]);
    switch (m_CBW.CBWCB[0]) {
        case 0x0:  // Test unit ready
        {
            if (!m_CDReady) {
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                bSenseKey = 2;
                bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
            } else {
                // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
                m_CSW.bmCSWStatus = bmCSWStatus;
            }
            SendCSW();
            break;
        }

        case 0x3:  // Request sense CMD
        {
            // This command is the host asking why the last command generated a check condition
            // We'll clear the reason after we've communicated it. If it's still an issue, we'll
            // throw another Check Condition afterwards
            bool desc = m_CBW.CBWCB[1] & 0x01;
            u8 blocks = (u8)(m_CBW.CBWCB[4]);

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Request Sense CMD, length = %d", blocks);

            u8 length = sizeof(TUSBCDRequestSenseReply);
            if (blocks < length)
                length = blocks;

            m_ReqSenseReply.bSenseKey = bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = bAddlSenseCode;
            m_ReqSenseReply.bAddlSenseCodeQual = bAddlSenseCodeQual;

            memcpy(&m_InBuffer, &m_ReqSenseReply, length);

            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, length);

            m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            m_nState = TCDState::SendReqSenseReply;

            // Reset response params after send
            bSenseKey = 0;
            bAddlSenseCode = 0;
            bAddlSenseCodeQual = 0;
            break;
        }

        case 0x12:  // Inquiry
        {
            int allocationLength = (m_CBW.CBWCB[3] << 8) | m_CBW.CBWCB[4];
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry %0x, allocation length %d", m_CBW.CBWCB[1], allocationLength);

            if ((m_CBW.CBWCB[1] & 0x01) == 0) {  // EVPD bit is 0: Standard Inquiry
                // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Standard Enquiry)");
                memcpy(&m_InBuffer, &m_InqReply, SIZE_INQR);
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, SIZE_INQR);
                m_nState = TCDState::DataIn;
                m_nnumber_blocks = 0;  // nothing more after this send
                m_CSW.bmCSWStatus = bmCSWStatus;
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
                            0x83   // Byte 6: Supported VPD Page Code: Device Identification
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
                        bSenseKey = 0x05;
                        bAddlSenseCode = 0x24;      // Invalid Field
                        bAddlSenseCodeQual = 0x00;  // In CDB
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
            m_ReadCapReply.nLastBlockAddr = htonl(GetLeadoutLBA() - 1);  // this value is the Start address of last recorded lead-out minus 1
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
                // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "Read (10)");
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
                block_size = data_block_size;  // set at SetDevice
                skip_bytes = data_skip_bytes;  // set at SetDevice;

                m_nbyteCount = m_CBW.dCBWDataTransferLength;

                // What is this?
                if (m_nnumber_blocks == 0) {
                    m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;
                }
                m_nState = TCDState::DataInRead;  // see Update() function
            } else {
                MLOGNOTE("handleSCSI Read(10)", "failed, %s", m_CDReady ? "ready" : "not ready");
                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                bSenseKey = 0x02;
                bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
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
                // 0030   00 00 10 f0 00 00 00 00 00 00                     ..........
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
                bSenseKey = 0x02;
                bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
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
            int format = m_CBW.CBWCB[2] & 0x0f;
            int startingTrack = m_CBW.CBWCB[6];
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read TOC with msf = %02x, starting track = %d, allocation length = %d, m_CDReady = %d", msf, startingTrack, allocationLength, m_CDReady);

            TUSBTOCData m_TOCData;
            TUSBTOCEntry* tocEntries;
            int numtracks = 0;
            int datalen = 0;

            // TODO implement formats. Currently we assume it's always 0x00

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
                    tocEntries[index].ADR_Control = 0x04;
                    if (trackInfo->track_mode == CUETrack_AUDIO)
                        tocEntries[index].ADR_Control = 0x00;
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
            tocEntries[index].ADR_Control = 0x16;
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
            unsigned int subq = (m_CBW.CBWCB[2] >> 6) & 0x01;
            unsigned int parameter_list = m_CBW.CBWCB[3];
            unsigned int track_number = m_CBW.CBWCB[6];
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
            int length = 0;

            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42), allocationLength = %d, msf = %u, subq = %u, parameter_list = 0x%02x, track_number = %u", allocationLength, msf, subq, parameter_list, track_number);

            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));

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
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Configuration with rt = %d and feature %lu", rt, feature);

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

                    memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
                    dataLength += sizeof(multiread);

                    memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                    dataLength += sizeof(cdread);

                    // Finally copy the header
                    header.dataLength = htonl(dataLength - 4);
                    memcpy(m_InBuffer, &header, sizeof(header));

                    // hexdump(m_InBuffer, dataLength);
                    break;
                }

                case 0x02:  // Only the features requested
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
                            break;
                        }
                        case 0x01: {  // Core
                            memcpy(m_InBuffer + dataLength, &core, sizeof(core));
                            dataLength += sizeof(core);
                            break;
                        }

                        case 0x02: {  // Morphing
                            memcpy(m_InBuffer + dataLength, &morphing, sizeof(morphing));
                            dataLength += sizeof(morphing);
                            break;
                        }
                        case 0x03: {  // Removable Medium
                            memcpy(m_InBuffer + dataLength, &mechanism, sizeof(mechanism));
                            dataLength += sizeof(mechanism);
                            break;
                        }
                        case 0x1d: {  // Multiread
                            memcpy(m_InBuffer + dataLength, &multiread, sizeof(multiread));
                            dataLength += sizeof(multiread);
                            break;
                        }
                        case 0x1e: {  // CD-Read
                            memcpy(m_InBuffer + dataLength, &cdread, sizeof(cdread));
                            dataLength += sizeof(cdread);
                            break;
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
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "SEEK");

            // Where to start reading (LBA)
            m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16) | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];

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

            // Play the audio
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {
                cdplayer->Play(start_lba, num_blocks);
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
                CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
                if (cdplayer) {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Play command sent");
                    if (m_nblock_address == 0xffffffff)
                        cdplayer->Resume();
                    else
                        cdplayer->Play(m_nblock_address, m_nnumber_blocks);
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
                CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
                if (cdplayer) {
                    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO (10) Play command sent");
                    if (m_nblock_address == 0xffffffff)
                        cdplayer->Resume();
                    else
                        cdplayer->Play(m_nblock_address, m_nnumber_blocks);
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

            int LLBAA = (m_CBW.CBWCB[1] >> 7) & 0x01;
            int DBD = (m_CBW.CBWCB[1] >> 6) & 0x01;
            int page = m_CBW.CBWCB[2] & 0x3F;
            int page_control = (m_CBW.CBWCB[2] >> 6) & 0x03;  // We'll ignore this for now
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) with LLBAA = %d, DBD = %d, page = %02x, allocationLength = %lu", LLBAA, DBD, page, allocationLength);

            int length = SIZE_MODE_SENSE10_HEADER;

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
                    codepage.numVolumeLevels = 0x00ff;
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
                        volume = cdplayer->GetVolume();
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
            bSenseKey = 0x5;  // Illegal/not supported
            bAddlSenseCode = 0x20;
            bAddlSenseCodeQual = 0x00;
            m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
            SendCSW();
            break;
        }
    }

    // Reset the status
    bmCSWStatus = CD_CSW_STATUS_OK;
}

// this function is called periodically from task level for IO
//(IO must not be attempted in functions called from IRQ)
void CUSBCDGadget::Update() {
    // MLOGNOTE ("CUSBCDGadget::Update", "entered");
    switch (m_nState) {
        case TCDState::DataInRead: {
            u64 offset = 0;
            int readCount = 0;
            if (m_CDReady) {
                // Google Gemini suggested this optimization. I don't think it's bad!
                offset = m_pDevice->Seek(block_size * m_nblock_address);
                if (offset != (u64)(-1)) {
                    // Cap at max 16 blocks. This is what a READ CD request will required
                    u32 blocks_to_read_in_batch = m_nnumber_blocks;
                    if (blocks_to_read_in_batch > 16) {
                        blocks_to_read_in_batch = 16;
                        m_nnumber_blocks -= 16;  // Update remaining for subsequent reads if needed
                        MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is %lu", blocks_to_read_in_batch, m_nnumber_blocks);
                    } else {
                        MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is now zero", blocks_to_read_in_batch);
                        m_nnumber_blocks = 0;
                    }

                    // Calculate total size of the batch read
                    u32 total_batch_size = blocks_to_read_in_batch * block_size;

                    MLOGDEBUG("UpdateRead", "Starting batch read for %lu blocks (total %lu bytes)", blocks_to_read_in_batch, total_batch_size);
                    // Perform the single large read
                    readCount = m_pDevice->Read(m_FileChunk, total_batch_size);
                    MLOGDEBUG("UpdateRead", "Read %d bytes in batch", readCount);

                    if (readCount < static_cast<int>(total_batch_size)) {
                        // Handle error: partial read
                        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                        bSenseKey = 0x04;       // hardware error
                        bAddlSenseCode = 0x11;  // UNRECOVERED READ ERROR
                        bAddlSenseCodeQual = 0x00;
                        SendCSW();
                        return;  // Exit if read failed
                    }

                    u8* dest_ptr = m_InBuffer;  // Pointer to current write position in m_InBuffer
                    u32 total_copied = 0;

                    // Iterate through the *read data* in memory
                    for (u32 i = 0; i < blocks_to_read_in_batch; ++i) {
                        // Calculate the starting point for the current block within the m_FileChunk
                        u8* current_block_start = m_FileChunk + (i * block_size);

                        // Copy only the portion after skip_bytes into the destination buffer
                        memcpy(dest_ptr, current_block_start + skip_bytes, transfer_block_size);
                        dest_ptr += transfer_block_size;
                        total_copied += transfer_block_size;
                        // m_nblock_address++; // This should be updated based on the *initial* block address + blocks_to_read_in_batch
                    }
                    // Update m_nblock_address after the batch read
                    m_nblock_address += blocks_to_read_in_batch;

                    MLOGDEBUG("UpdateRead", "Total copied is %lu", total_copied);

                    // Adjust m_nbyteCount based on how many bytes were copied
                    m_nbyteCount -= total_copied;
                    m_nState = TCDState::DataIn;

                    // Begin USB transfer of the in-buffer (only valid data)
                    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, total_copied);
                }
            }
            if (!m_CDReady || offset == (u64)(-1)) {
                MLOGERR("UpdateRead", "failed, %s, offset=%llu",
                        m_CDReady ? "ready" : "not ready", offset);
                m_CSW.bmCSWStatus = CD_CSW_STATUS_PHASE_ERR;
                bSenseKey = 0x02;           // Not Ready
                bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                SendCSW();
            }
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
