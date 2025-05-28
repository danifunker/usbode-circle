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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <assert.h>
#include <circle/koptions.h>
#include <circle/logger.h>
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
        0x04da, // Panasonic
        0x0d01,	// CDROM
        //USB_GADGET_VENDOR_ID,
        //USB_GADGET_DEVICE_ID_CD,
        0x000,    // bcdDevice
        1, 2, 0,  // strings
        1         // num configurations
};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptor =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof s_ConfigurationDescriptor,
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
            0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            static_cast<uint16_t>(CKernelOptions::Get()->GetUSBFullSpeed() ? 64 : 512),  // wMaxPacketSize
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            static_cast<uint16_t>(CKernelOptions::Get()->GetUSBFullSpeed() ? 64 : 512),  // wMaxPacketSize
            0                                                                            // bInterval
        }};

const char* const CUSBCDGadget::s_StringDescriptor[] =
    {
        "\x04\x03\x09\x04",  // Language ID
        "USBODE",
        "USB Optical Disk Emulator"};

CUSBCDGadget::CUSBCDGadget(CInterruptSystem* pInterruptSystem, CCueBinFileDevice* pDevice)
    : CDWUSBGadget(pInterruptSystem,
                   CKernelOptions::Get()->GetUSBFullSpeed() ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr} {
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "entered");
    if (pDevice)
        SetDevice(pDevice);
}

CUSBCDGadget::~CUSBCDGadget(void) {
    assert(0);
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
                *pLength = sizeof s_ConfigurationDescriptor;
                return &s_ConfigurationDescriptor;
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
    m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
        reinterpret_cast<const TUSBEndpointDescriptor*>(
            &s_ConfigurationDescriptor.EndpointOut),
        this);
    assert(m_pEP[EPOut]);

    assert(!m_pEP[EPIn]);
    m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
        reinterpret_cast<const TUSBEndpointDescriptor*>(
            &s_ConfigurationDescriptor.EndpointIn),
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
        bAddlSenseCode = 0x28;      // NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED
        bAddlSenseCodeQual = 0x00;  // NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED
    }

    m_pDevice = dev;

    cueParser = CUEParser(m_pDevice->GetCueSheet());  // FIXME. Ensure cuesheet is not null or empty

    MLOGNOTE("CUSBCDGadget::InitDevice", "entered");

    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();

    m_CDReady = true;
    MLOGNOTE("CUSBCDGadget::InitDeviceSize", "Block size is %d, m_CDReady = %d", block_size, m_CDReady);
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
    cueParser.restart();
    const CUETrackInfo* trackInfo = nullptr;
    const CUETrackInfo* lastTrackInfo = nullptr;
    int track = 0;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "At track number = %d, track_start = %d", trackInfo->track_number, trackInfo->track_start);
        if (trackInfo->track_start > lba) {
            MLOGNOTE("CUSBCDGadget::GetTrackInfoForLBA", "LBA %lu is in track %s with sector size of %lu", lba, track, trackInfo->sector_length);
            return lastTrackInfo;
        }

        lastTrackInfo = trackInfo;
    }
    return nullptr;
}

u32 CUSBCDGadget::GetLeadoutLBA() {
    const CUETrackInfo* trackInfo = nullptr;
    const CUETrackInfo* lastTrackInfo = nullptr;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        lastTrackInfo = trackInfo;
    }

    u64 lastTrackBlocks = (m_pDevice->GetSize() - lastTrackInfo->file_offset) / lastTrackInfo->sector_length;
    u32 ret = lastTrackInfo->data_start + lastTrackBlocks;
    // MLOGNOTE ("CUSBCDGadget::GetLeadoutLBA", "device size is %llu, last track file offset is %llu, last track sector_length is %lu, last track data_start is %lu, lastTrackBlocks = %lu, returning = %lu", m_pDevice->GetSize(), lastTrackInfo->file_offset, lastTrackInfo->sector_length, lastTrackInfo->data_start, lastTrackBlocks, ret);
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
                        m_ReqSenseReply.bSenseKey = 0x02;
                        m_ReqSenseReply.bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                        m_ReqSenseReply.bAddlSenseCodeQual = 0x00;  // FIXME CAUSE NOT REPORTABLE
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

            default: {
                MLOGERR("onXferCmplt", "dir=out, unhandled state = %i", m_nState);
                assert(0);
                break;
            }
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

u32 CUSBCDGadget::lba_to_msf(u32 lba) {
    lba = lba + 150;  // MSF values are offset by 2mins. Weird

    u8 minutes = lba / (75 * 60);
    u8 seconds = (lba / 75) % 60;
    u8 frames = lba % 75;
    u8 reserved = 0;

    return (frames << 24) | (seconds << 16) | (minutes << 8) | reserved;
}

u32 CUSBCDGadget::GetAddress(u32 lba, int msf) {
    u32 address = lba;
    if (msf)
        return lba_to_msf(lba);
    return htonl(address);
}

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
                bAddlSenseCodeQual = 0x00;  // FIXME CAUSE NOT REPORTABLE
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
            // TODO: Add some code to store the reason why we threw an error previously
            // TODO: For now, we'll assume this is always because we changed CD image

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
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry %0x, allocation length %d", m_CBW.CBWCB[1], allocationLength);

            if ((m_CBW.CBWCB[1] & 0x01) == 0) {  // EVPD bit is 0: Standard Inquiry
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Standard Enquiry)");
                memcpy(&m_InBuffer, &m_InqReply, SIZE_INQR);
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, SIZE_INQR);
                m_nState = TCDState::DataIn;
                m_nnumber_blocks = 0;  // nothing more after this send
                m_CSW.bmCSWStatus = bmCSWStatus;
            } else {  // EVPD bit is 1: VPD Inquiry
                MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (VPD Inquiry)");
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
                        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unit Serial number Page)");

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
                    } break;
                    default:  // Unsupported VPD Page
                        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Inquiry (Unsupported Page)");
                        m_nState = TCDState::DataIn;
                        m_nnumber_blocks = 0;  // nothing more after this send

                        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;  // FIXME throw CD_CSW_STATUS_FAIL but implement sense response
                        bSenseKey = 0x05;
                        bAddlSenseCode = 0x24;      // Invalid Field
                        bAddlSenseCodeQual = 0x00;  // In CDB
                        SendCSW();
                        break;
                }
            }
            break;
        }

        case 0x1A:  // Mode sense (6)
        {
            // FIXME
            memcpy(&m_InBuffer, &m_ModeSenseReply, SIZE_MODEREP);
            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, SIZE_MODEREP);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x1B:  // Start/stop unit
        {
            m_CDReady = (m_CBW.CBWCB[4] >> 1) == 0;
            MLOGNOTE("HandleSCSI", "start/stop, %s", m_CDReady ? "ready" : "not ready");
            m_CSW.bmCSWStatus = bmCSWStatus;
            SendCSW();
            break;
        }

        case 0x1E:  // PREVENT ALLOW MEDIUM REMOVAL
        {
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
                // Assume the data being requested is in track 1
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
                bAddlSenseCodeQual = 0x00;  // FIXME CAUSE NOT REPORTABLE
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
                bAddlSenseCodeQual = 0x00;  // FIXME CAUSE NOT REPORTABLE
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
            int msf = m_CBW.CBWCB[1] & 0x02;
            int format = m_CBW.CBWCB[2] & 0x0f;
            int startingTrack = m_CBW.CBWCB[5];
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Read TOC with msf = %02x, starting track = %d, allocation length = %d, m_CDReady = %d", msf, startingTrack, allocationLength, m_CDReady);

            TUSBTOCData m_TOCData;
            TUSBTOCEntry* tocEntries;
            int numtracks = 0;
            int datalen = 0;

            if (startingTrack == 0 && allocationLength > 12) {
                // Host wants a full TOC

                const CUETrackInfo* trackInfo = nullptr;
                int lastTrackNumber = GetLastTrackNumber();

                // Header
                m_TOCData.FirstTrack = 0x01;
                m_TOCData.LastTrack = lastTrackNumber;
                datalen = SIZE_TOC_DATA;

                // Populate the track entries
                tocEntries = new TUSBTOCEntry[lastTrackNumber + 1];

                int index = 0;
                cueParser.restart();
                while ((trackInfo = cueParser.next_track()) != nullptr) {
                    // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "Adding at index %d: track number = %d, data_start = %d, start lba or msf %d", index, trackInfo->track_number, trackInfo->data_start, GetAddress(trackInfo->data_start, msf));
                    tocEntries[index].ADR_Control = 0x14;
                    if (trackInfo->track_mode == CUETrack_AUDIO)
                        tocEntries[index].ADR_Control = 0x11;
                    tocEntries[index].reserved = 0x00;
                    tocEntries[index].TrackNumber = trackInfo->track_number;
                    tocEntries[index].reserved2 = 0x00;
                    tocEntries[index].address = GetAddress(trackInfo->data_start, msf);
                    datalen += SIZE_TOC_ENTRY;
                    ++index;
                }

                // Lead-Out LBA
                u32 leadOutLBA = GetLeadoutLBA();
                tocEntries[index].ADR_Control = 0x16;
                tocEntries[lastTrackNumber].reserved = 0x00;
                tocEntries[lastTrackNumber].TrackNumber = 0xAA;
                tocEntries[lastTrackNumber].reserved2 = 0x00;
                tocEntries[lastTrackNumber].address = GetAddress(leadOutLBA, msf);
                datalen += SIZE_TOC_ENTRY;

                numtracks = lastTrackNumber + 1;

            } else {
                // Host wants a specific track

                // Header
                datalen = SIZE_TOC_DATA;
                m_TOCData.FirstTrack = 0x01;
                m_TOCData.LastTrack = 0x01;
                numtracks = 1;

                // TOC Entries
                tocEntries = new TUSBTOCEntry[1];

                if (startingTrack == 0xAA) {
                    // Host wants a Lead Out Track

                    // Check number of tracks
                    u32 leadOutLBA = GetLeadoutLBA();
                    tocEntries[0].ADR_Control = 0x16;
                    tocEntries[0].reserved = 0x00;
                    tocEntries[0].TrackNumber = 0xAA;
                    tocEntries[0].reserved2 = 0x00;
                    tocEntries[0].address = GetAddress(leadOutLBA, msf);
                    datalen += SIZE_TOC_ENTRY;
                } else {
                    // Host wants a specific track

                    const CUETrackInfo* trackInfo = nullptr;
                    cueParser.restart();
                    while ((trackInfo = cueParser.next_track()) != nullptr) {
                        // MLOGDEBUG ("CUSBCDGadget::HandleSCSICommand", "Single TOC - Found track count = %d, data_start = %d, track_start = %d, sector_length = %d", trackInfo->track_number, trackInfo->data_start, trackInfo->track_start, trackInfo->sector_length);
                        if (trackInfo->track_number - 1 == startingTrack) {
                            break;
                        }
                    }

                    tocEntries[0].ADR_Control = 0x14;
                    tocEntries[0].reserved = 0x00;
                    tocEntries[0].TrackNumber = startingTrack;
                    tocEntries[0].reserved2 = 0x00;
                    tocEntries[0].address = GetAddress(trackInfo->data_start, msf);
                    datalen += SIZE_TOC_ENTRY;
                }
            }

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
            int allocationLength = (m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42), allocationLength = %d", allocationLength);

            static const uint8_t stubSubChannelResponse[] = {
                // Header (Bytes 0-3)
                0x00, 0x0A,  // Data Length (0x0A = 10 bytes for Q-channel data)
                0x00,        // Reserved
                0x13,        // Audio Status: 0x13 = Audio play operation stopped

                // Q-Channel Data (Bytes 4-15, as described below)
                0x01,  // Current Track Number (BCD: 01)
                0x01,  // Current Index Number (BCD: 01)
                0x00,  // Absolute MSF (Minutes) (BCD: 00)
                0x00,  // Absolute MSF (Seconds) (BCD: 00)
                0x00,  // Absolute MSF (Frames)  (BCD: 00)
                0x00,  // Reserved (Control flags/ADR/etc. - usually 0 for audio)
                0x00,  // Track MSF (Minutes)    (BCD: 00)
                0x00,  // Track MSF (Seconds)    (BCD: 00)
                0x00,  // Track MSF (Frames)     (BCD: 00)
                0x00,  // Reserved (Q-channel CRC MSB)
                0x00,  // Reserved (Q-channel CRC LSB)

                // Remainder of 96 bytes (Bytes 16-95) - typically all zeros if not used
                // These bytes are for P/R/S/T/U/V/W sub-channel data or reserved.
                // For a simple stub, zeroing them out is perfectly fine.
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // ... and so on for 80 more bytes
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

            int datalen = sizeof(stubSubChannelResponse);
            if (allocationLength < datalen)
                datalen = allocationLength;

            // Copy stub data into IN buffer
            memcpy(m_InBuffer, stubSubChannelResponse, datalen);

            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       m_InBuffer, datalen);

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
            // MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Get Configuration with rt = %d and feature %lu", rt, feature);

            switch (rt) {
                case 0x00:
                    memcpy(m_InBuffer, &m_GetConfigurationReply, SIZE_GET_CONFIGURATION_REPLY);
                    break;
                case 0x01:
                    memcpy(m_InBuffer, &m_GetConfigurationReply, SIZE_GET_CONFIGURATION_REPLY);
                    break;
                case 0x02:
                    switch (feature) {
                        case 0x02: {
                            // Profile list
                            memcpy(m_InBuffer, &m_GetConfigurationReply, SIZE_GET_CONFIGURATION_REPLY);
                            break;
                        }
                        case 0x1e: {
                            // CD Read
                            u8 bytes[] = {0x0, 0x0, 0x0, 0xc, 0x0, 0x0, 0x0, 0x8, 0x0, 0x1e, 0x9, 0x4, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
                            memcpy(m_InBuffer, bytes, sizeof(bytes));
                            break;
                        }
                        default: {
                            u8 bytes[] = {0x0, 0x0, 0x0, 0xc, 0x0, 0x0, 0x0, 0x8, 0x0, 0x1e, 0x9, 0x4, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
                            memcpy(m_InBuffer, bytes, sizeof(bytes));
                            break;
                        }
                    }
                    break;
                case 0x10:
                    // The Feature Header and the Feature Descriptor identified by Starting Feature Number shall be returned.
                    // If the Drive does not support the specified feature, only the Feature Header shall be returned.
                    memcpy(m_InBuffer, &m_GetConfigurationReply, SIZE_GET_CONFIGURATION_REPLY);
                    break;
                default:
                    memcpy(m_InBuffer, &m_GetConfigurationReply, SIZE_GET_CONFIGURATION_REPLY);
                    break;
            }

            // Set response length
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            int length = sizeof(TUSBCDGetConfigurationReply);
            if (allocationLength < length)
                length = allocationLength;

            memcpy(m_InBuffer, &m_GetConfigurationReply, length);
            m_nnumber_blocks = 0;  // nothing more after this send
            m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
            m_nState = TCDState::DataIn;
            m_CSW.bmCSWStatus = bmCSWStatus;
            break;
        }

        case 0x47:  // PLAY AUDIO MSF
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO MSF");

            // FIXME: implement

            m_CSW.bmCSWStatus = bmCSWStatus;
            m_ReqSenseReply.bSenseKey = bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = bAddlSenseCode;
            break;
        }

        case 0x48:  // PLAY AUDIO TRACK/INDEX
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PLAY AUDIO TRACK/INDEX");

            // FIXME: implement

            m_CSW.bmCSWStatus = bmCSWStatus;
            m_ReqSenseReply.bSenseKey = bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = bAddlSenseCode;
            break;
        }

        case 0x4B:  // PAUSE/RESUME
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "PAUSE/RESUME");

            // FIXME: implement

            m_CSW.bmCSWStatus = bmCSWStatus;
            m_ReqSenseReply.bSenseKey = bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = bAddlSenseCode;
            break;
        }

        case 0x55:  // Mode Select (10)
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Select (10)");

            // FIXME: implement

            m_CSW.bmCSWStatus = bmCSWStatus;
            m_ReqSenseReply.bSenseKey = bSenseKey;
            m_ReqSenseReply.bAddlSenseCode = bAddlSenseCode;
            break;
        }

        case 0x5a:  // Mode Sense (10)
        {
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10)");

            int LLBAA = m_CBW.CBWCB[1] & 0x3F;
            int DBD = (m_CBW.CBWCB[1] >> 6) & 0x03;
            int page = m_CBW.CBWCB[2];
            u16 allocationLength = m_CBW.CBWCB[7] << 8 | (m_CBW.CBWCB[8]);
            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10) with LLBAA = %d, DBD = %d, page = %02x, allocationLength = %lu", LLBAA, DBD, page, allocationLength);

            // TODO: Implement proper mode sense response
            // TODO: assume 0x2a for now

            // Trim the reply length according to what the host requested
            int length = sizeof(TUSBCDModeSense10Reply);
            if (allocationLength < length)
                length = allocationLength;

            MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Mode Sense (10), Sending response with length %d", length);
            m_ModeSense10Reply.mediumType = GetMediumType();
            memcpy(m_InBuffer, &m_ModeSense10Reply, length);
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

                // Seek to correct position in underlying storage file
                /*
                MLOGDEBUG("UpdateRead", "Seeking %d block_size * %lu address", block_size, m_nblock_address);
                offset=m_pDevice->Seek(block_size*m_nblock_address);
                if(offset!=(u64)(-1))
                {
                        // Cap at max 16 blocks
                        u32 blocks = m_nnumber_blocks;
                        if (blocks > 16) {
                            blocks = 16;
                            m_nnumber_blocks -= 16;
                            MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is %lu", blocks, m_nnumber_blocks);
                        } else {
                            MLOGDEBUG("UpdateRead", "Blocks is now %lu, remaining blocks is now zero", blocks);
                            m_nnumber_blocks = 0;
                        }

                        u8* dest_ptr = m_InBuffer; // Pointer to current write position in m_InBuffer
                        u32 total_read = 0;

                        MLOGDEBUG("UpdateRead", "Starting read for %lu blocks with transfer_block_size %lu", blocks, transfer_block_size);

                        for (; blocks > 0; blocks--) {
                            MLOGDEBUG("UpdateRead", "Blocks left %lu of size %d", blocks, block_size);
                            readCount = m_pDevice->Read(m_OneSector, block_size);
                            MLOGDEBUG("UpdateRead", "Read %d bytes", readCount);

                            if (readCount < static_cast<int>(block_size)) {
                                m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                                bSenseKey = 0x04; // hardware error
                                bAddlSenseCode = 0x11; // UNRECOVERED READ ERROR
                                bAddlSenseCode = 0x00;
                                SendCSW();
                                break;
                            }

                            // Copy only the portion after skip_bytes into the destination buffer
                            memcpy(dest_ptr, m_OneSector + skip_bytes, transfer_block_size);
                            dest_ptr += transfer_block_size;
                            total_read += transfer_block_size;
                            m_nblock_address++;
                        }

                        MLOGDEBUG("UpdateRead", "Total read is %lu", total_read);

                        // Adjust m_nbyteCount based on how many bytes were copied
                        m_nbyteCount -= total_read;
                        m_nState = TCDState::DataIn;

                        // Begin USB transfer of the in-buffer (only valid data)
                        m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, total_read);

                }
                        */
            }
            if (!m_CDReady || offset == (u64)(-1)) {
                MLOGERR("UpdateRead", "failed, %s, offset=%llu",
                        m_CDReady ? "ready" : "not ready", offset);
                m_ReqSenseReply.bSenseKey = 2;
                m_ReqSenseReply.bAddlSenseCode = 1;
                m_CSW.bmCSWStatus = CD_CSW_STATUS_PHASE_ERR;
                bSenseKey = 0x02;           // Not Ready
                bAddlSenseCode = 0x04;      // LOGICAL UNIT NOT READY
                bAddlSenseCodeQual = 0x00;  // CAUSE NOT REPORTABLE
                SendCSW();
            }
            break;
        }

        default:
            break;
    }
}
