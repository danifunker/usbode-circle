//
// scsi_inquiry.cpp
//
// SCSI Inquiry, Mode Sense, Request Sense
//
#include <usbcdgadget/scsi_inquiry.h>
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <cdplayer/cdplayer.h>
#include <circle/sched/scheduler.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

void SCSIInquiry::Inquiry(CUSBCDGadget *gadget)
{
    int allocationLength = (gadget->m_CBW.CBWCB[3] << 8) | gadget->m_CBW.CBWCB[4];
    CDROM_DEBUG_LOG("SCSIInquiry::Inquiry", "Inquiry %0x, allocation length %d", gadget->m_CBW.CBWCB[1], allocationLength);

    if ((gadget->m_CBW.CBWCB[1] & 0x01) == 0)
    { // EVPD bit is 0: Standard Inquiry
        CDROM_DEBUG_LOG("SCSIInquiry::Inquiry", "Inquiry (Standard Enquiry)");

        // Set response length
        int datalen = SIZE_INQR;
        if (allocationLength < datalen)
            datalen = allocationLength;

        memcpy(gadget->m_InBuffer, &gadget->m_InqReply, datalen);
        gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, datalen);
        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
        gadget->m_nnumber_blocks = 0; // nothing more after this send
        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    }
    else
    { // EVPD bit is 1: VPD Inquiry
        CDROM_DEBUG_LOG("SCSIInquiry::Inquiry", "Inquiry (VPD Inquiry)");
        u8 vpdPageCode = gadget->m_CBW.CBWCB[2];
        switch (vpdPageCode)
        {
        case 0x00: // Supported VPD Pages
        {
            CDROM_DEBUG_LOG("SCSIInquiry::Inquiry", "Inquiry (Supported VPD Pages)");

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

            memcpy(gadget->m_InBuffer, &SupportedVPDPageReply, sizeof(SupportedVPDPageReply));
            gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                             gadget->m_InBuffer, datalen);
            gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            gadget->m_nnumber_blocks = 0; // nothing more after this send
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }

        case 0x80: // Unit Serial Number Page
        {
            CDROM_DEBUG_LOG("SCSIInquiry::Inquiry", "Inquiry (Unit Serial number Page)");

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

            memcpy(gadget->m_InBuffer, &UnitSerialNumberReply, sizeof(UnitSerialNumberReply));
            gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                             gadget->m_InBuffer, datalen);
            gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            gadget->m_nnumber_blocks = 0; // nothing more after this send
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
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

            memcpy(gadget->m_InBuffer, &DeviceIdentificationReply, sizeof(DeviceIdentificationReply));
            gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                             gadget->m_InBuffer, datalen);
            gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            gadget->m_nnumber_blocks = 0; // nothing more after this send
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }

        default: // Unsupported VPD Page
            MLOGNOTE("SCSIInquiry::Inquiry", "Inquiry (Unsupported Page)");
            gadget->m_nnumber_blocks = 0;           // nothing more after this send
            gadget->setSenseData(0x05, 0x24, 0x00); // Invalid Field in CDB
            gadget->sendCheckCondition();
            break;
        }
    }
}

void SCSIInquiry::RequestSense(CUSBCDGadget *gadget)
{
    MLOGNOTE("SCSIInquiry::RequestSense", "*** CALLED *** mediaState=%d", (int)gadget->m_mediaState);
    
    // CRITICAL: Initialize the entire structure first!
    memset(&gadget->m_ReqSenseReply, 0, sizeof(TUSBCDRequestSenseReply));
    gadget->m_ReqSenseReply.bErrCode = 0x70;        // Current error, fixed format
    gadget->m_ReqSenseReply.bAddlSenseLen = 0x0A;   // 10 additional bytes
    
    u8 blocks = (u8)(gadget->m_CBW.CBWCB[4]);
    u8 length = sizeof(TUSBCDRequestSenseReply);
    if (blocks < length)
        length = blocks;
    
    gadget->m_ReqSenseReply.bSenseKey = gadget->m_SenseParams.bSenseKey;
    gadget->m_ReqSenseReply.bAddlSenseCode = gadget->m_SenseParams.bAddlSenseCode;
    gadget->m_ReqSenseReply.bAddlSenseCodeQual = gadget->m_SenseParams.bAddlSenseCodeQual;
    
    CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                    "REQUEST SENSE: mediaState=%d, sense=%02x/%02x/%02x, length=%d -> reporting to host",
                    (int)gadget->m_mediaState,
                    gadget->m_SenseParams.bSenseKey, gadget->m_SenseParams.bAddlSenseCode, gadget->m_SenseParams.bAddlSenseCodeQual,
                    length);
    
    memcpy(gadget->m_InBuffer, &gadget->m_ReqSenseReply, length);
    
    // Debug: dump first 8 bytes of buffer to verify format
    CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                    "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x",
                    gadget->m_InBuffer[0], gadget->m_InBuffer[1], gadget->m_InBuffer[2], gadget->m_InBuffer[3],
                    gadget->m_InBuffer[4], gadget->m_InBuffer[5], gadget->m_InBuffer[6], gadget->m_InBuffer[7]);
    
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                       gadget->m_InBuffer, length);
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->m_nState = CUSBCDGadget::TCDState::SendReqSenseReply;
    
    if (gadget->m_mediaState == CUSBCDGadget::MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        gadget->clearSenseData();
        gadget->m_mediaState = CUSBCDGadget::MediaState::MEDIUM_PRESENT_READY;
        gadget->bmCSWStatus = CD_CSW_STATUS_OK;
    }
    else
    {
        gadget->clearSenseData();
    }
}

void SCSIInquiry::FillModePage(CUSBCDGadget *gadget, u8 page, u8 *buffer, int &length)
{
    switch (page)
    {
    case 0x01:
    {
        // Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x01 response");

        ModePage0x01Data codepage;
        memset(&codepage, 0, sizeof(codepage));

        // Set the required fields to match LG drive
        codepage.pageCodeAndPS = 0x01;          // Page code 0x01
        codepage.pageLength = 0x0a;             // 10 bytes of parameters
        codepage.errorRecoveryBehaviour = 0x80; // AWRE bit set (automatic write reallocation enabled)
        codepage.readRetryCount = 0xc0;         // Read retry count

        // Copy the header & Code Page
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }
    case 0x05:
    {
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x05 (Write Parameters)");
        struct ModePage0x05Data
        {
            u8 pageCodeAndPS;
            u8 pageLength;
            u8 writeType : 4;
            u8 testWrite : 1;
            u8 linkSize : 2;
            u8 bufferUnderrun : 1;
            u8 trackMode : 4;
            u8 copy : 1;
            u8 fp : 1;
            u8 multiSession : 2;
            u8 dataBlockType : 4;
            u8 reserved1 : 4;
            u8 linkSize2;
            u8 reserved2;
            u8 hostAppCode : 6;
            u8 reserved3 : 2;
            u8 sessionFormat;
            u8 reserved4;
            u32 packetSize;
            u16 audioPauseLength;
            u8 mcn[16];
            u8 isrc[16];
            u8 subHeader[4];
            u8 vendor[4];
        } PACKED;

        ModePage0x05Data codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x05;
        codepage.pageLength = 0x32; // 50 bytes
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }
    case 0x0D:
    { // CD Device Parameters
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage",
                        "MODE SENSE Page 0x0D (CD Device Parameters)");

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

        memcpy(buffer + length, &codePage, sizeof(codePage));
        length += sizeof(codePage);
        break;
    }

    case 0x08:
    {
        // Mode Page 0x08 (Caching)
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x08 (Caching)");

        ModePage0x08Data codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x08;
        codepage.pageLength = 0x12;
        codepage.cachingFlags = 0x00; // RCD=0, WCE=0

        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x1a:
    {
        // Mode Page 0x1A (Power Condition)
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x1a response");

        // Define our Code Page
        ModePage0x1AData codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x1a;
        codepage.pageLength = 0x0a;

        // Copy the header & Code Page
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x2a:
    {
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x2a response");

        if (gadget->m_USBTargetOS == USBTargetOS::Apple)
        {
            // --- APPLE SPECIFIC LOGIC (Mimic Sony Spressa) ---
            ModePage0x2AData_APPLE codepage;
            memset(&codepage, 0, sizeof(codepage));

            codepage.pageCodeAndPS = 0x2a;
            codepage.pageLength = 0x14; // 20 bytes payload (Sony Match)

            // Capabilities: 07 07 71 63 (Sony Match)
            codepage.capabilityBits[0] = 0x00; // Read: CD-R, CD-E, Method 2
            codepage.capabilityBits[1] = 0x00; // Write: None (0 is safer for emulation)
            codepage.capabilityBits[2] = 0x71; // Features 1 (Includes M2F1, M2F2, Audio)
            codepage.capabilityBits[3] = 0x63; // Features 2 (CD-DA)

            // Mechanism State: 0x28 (Tray, Eject supported, No Locking)
            codepage.capabilityBits[4] = 0x28;
            codepage.capabilityBits[5] = 0x03; // Audio control

            // Speed / Buffer (Mimic Sony Spressa)
            codepage.maxSpeed = htons(1378);
            codepage.numVolumeLevels = htons(0x0100);
            codepage.bufferSize = htons(1378);
            codepage.currentSpeed = htons(1378);

            // Tail bytes (Sony specific padding/values)
            codepage.reserved1[0] = 0x00;
            codepage.reserved1[1] = 0x00;
            codepage.maxReadSpeed = htons(1378);
            codepage.reserved2[0] = 0x02;
            codepage.reserved2[1] = 0xc2;

            memcpy(buffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
        }
        else
        {
            // --- EXISTING LOGIC (For Windows/Linux compatibility) ---
            ModePage0x2AData codepage;
            memset(&codepage, 0, sizeof(codepage));
            codepage.pageCodeAndPS = 0x2a;
            codepage.pageLength = 0x42;

            // Capability bits (6 bytes) - dynamic based on media type
            // Byte 0: bit0=DVD-ROM, bit1=DVD-R, bit2=DVD-RAM, bit3=CD-R, bit4=CD-RW, bit5=Method2
            codepage.capabilityBits[0] = 0x3F; // Read: CD-R, CD-RW, Method 2
            codepage.capabilityBits[1] = 0x37; // All writable types
            codepage.capabilityBits[2] = 0xf1; // AudioPlay, composite audio/video, digital port 2, Mode 2 Form 2, Mode 2 Form 1
            codepage.capabilityBits[3] = 0x77; // CD-DA Commands Supported, CD-DA Stream is accurate
            codepage.capabilityBits[4] = 0x29; // Tray loading mechanism, eject supported, lock supported
            codepage.capabilityBits[5] = 0x23; // No separate channel volume, no separate channel mute

            // Speed and buffer info
            codepage.obsolete1 = htons(1378);          // Was Read Speed, set to 8x
            codepage.numVolumeLevels = htons(0x0100); // 256 volume levels
            codepage.bufferSize = htons(0x0040);      // Set to 64 KB buffer size
            codepage.obsolete2 = htons(1378);      // Was current speed
            codepage.obsolete5 = htons(1378);      // Was max read speed

            codepage.reserved1 = 0x00;
            codepage.bckFlags = 0x10;

            codepage.obsolete3 = htons(0x108a);
            codepage.obsolete4 = htons(0x108a);
            codepage.currentWriteSpeed = htons(0x0001);

            codepage.reserved2 = 0x00;
            codepage.reserved3 = 0x00;
            codepage.reserved4 = 0x0000;

            codepage.obsolete5 = htons(0x108a);
            codepage.reserved5 = 0x00;
            codepage.rotationControl = 0x03;

            memcpy(buffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
        }
        break;
    }

    case 0x2D:
    {
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x2D (CD Timeout & Protect)");
        struct ModePage0x2DData
        {
            u8 pageCodeAndPS;
            u8 pageLength;
            u8 reserved1;
            u8 inactivityTimerMultiplier;
            u16 swpp;
            u16 disp;
            u16 group1Timeout;
            u16 group2Timeout;
        } PACKED;

        ModePage0x2DData codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x2D;
        codepage.pageLength = 0x0A; // 10 bytes
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x0e:
    {
        // Mode Page 0x0E (CD Audio Control Page)
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x0e response");

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
        codepage.pageLength = 0x0e;
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
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x1c:
    {
        // Mode Page 0x1C (Informational Exceptions Control)
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x1c response");

        ModePage0x1CData codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x1c;
        codepage.pageLength = 0x0a;
        codepage.flags = 0x00; // No special flags
        codepage.mrie = 0x00;  // No reporting

        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }
    case 0x30:
    {
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x30 (Apple Vendor)");
        ModePage0x30Data codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x30;
        codepage.pageLength = 0x14; // 20 bytes for "APPLE COMPUTER, INC."
        memcpy(codepage.appleID, "APPLE COMPUTER, INC.", 20);
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x31:
    {
        // Page 0x31 - Apple vendor-specific page
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x31 (Apple vendor page)");
        ModePage0x31Data codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x31;
        codepage.pageLength = 0x14; // 20 bytes for "APPLE COMPUTER, INC."
        memcpy(codepage.appleID, "APPLE COMPUTER, INC.", 20);
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    case 0x4e:
    {
        // Page 0x4e - Mac OS 9 queries this, but Sony drive returns page 0x0e
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense 0x4e (returns 0x0e with max volume)");
        ModePage0x4eData codepage;
        memset(&codepage, 0, sizeof(codepage));
        codepage.pageCodeAndPS = 0x0e; // Return page 0x0e, not 0x4e
        codepage.pageLength = 0x0e;    // 14 bytes
        codepage.flags = 0x02;         // SOTC bit set
        codepage.port0Channel = 0x0f;  // Max channel
        codepage.port0Volume = 0xff;   // Max volume
        codepage.port1Channel = 0x0f;  // Max channel
        codepage.port1Volume = 0xff;   // Max volume
        memcpy(buffer + length, &codepage, sizeof(codepage));
        length += sizeof(codepage);
        break;
    }

    default:
        // We don't support this code page
        CDROM_DEBUG_LOG("SCSIInquiry::FillModePage", "Mode Sense unsupported page 0x%02x", page);
        break;
    }
}

void SCSIInquiry::ModeSense6(CUSBCDGadget *gadget)
{
    int cdbSize = 6;
    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense6", "Mode Sense (%d)", cdbSize);

    int page = gadget->m_CBW.CBWCB[2] & 0x3f;
    int page_control = (gadget->m_CBW.CBWCB[2] >> 6) & 0x03;
    u16 allocationLength = gadget->m_CBW.CBWCB[4];

    // We don't support saved values
    if (page_control == 0x03)
    {
        gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
        gadget->setSenseData(0x05, 0x39, 0x00);   // Illegal Request, Saving parameters not supported
        gadget->sendCheckCondition();
        return;
    }

    int length = 0;

    // Define our response header
    ModeSense6Header reply_header;
    memset(&reply_header, 0, sizeof(reply_header));
    reply_header.mediumType = CDUtils::GetMediumType(gadget);
    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense6", "Medium Type: 0x%02x", reply_header.mediumType);
    length += sizeof(reply_header);
    memcpy(gadget->m_InBuffer, &reply_header, sizeof(reply_header));

    // Process pages
    if (page == 0x3f) // All pages
    {
        CDROM_DEBUG_LOG("SCSIInquiry::ModeSense6", "Mode Sense All Mode Pages");
        // Original MODE SENSE (6) order: 01, 05, 0D, 08, 1a, 2a, 2D, 0e, 1c, 30, 31
        u8 pages[] = {0x01, 0x05, 0x0D, 0x08, 0x1a, 0x2a, 0x2D, 0x0e, 0x1c, 0x30, 0x31, 0x4e};
        for (u8 p : pages)
            FillModePage(gadget, p, gadget->m_InBuffer, length);
    }
    else
    {
        FillModePage(gadget, page, gadget->m_InBuffer, length);
    }

    // If unsupported page was requested (length didn't increase)
    if (length == sizeof(ModeSense6Header))
    {
        if (page != 0x3f)
        {
            gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN COMMAND PACKET
            gadget->sendCheckCondition();
            return;
        }
    }

    // Update header with data length
    ModeSense6Header *reply_header_ptr = (ModeSense6Header *)gadget->m_InBuffer;
    reply_header_ptr->modeDataLength = (length - 1);

    // Trim the reply length according to what the host requested
    if (allocationLength < length)
        length = allocationLength;

    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense6", "Mode Sense (%d), Sending response with length %d", cdbSize, length);

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIInquiry::ModeSense10(CUSBCDGadget *gadget)
{
    int cdbSize = 10;
    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense10", "Mode Sense (%d)", cdbSize);

    int page = gadget->m_CBW.CBWCB[2] & 0x3f;
    int page_control = (gadget->m_CBW.CBWCB[2] >> 6) & 0x03;
    u16 allocationLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);

    // We don't support saved values
    if (page_control == 0x03)
    {
        gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
        gadget->setSenseData(0x05, 0x39, 0x00);   // Illegal Request, Saving parameters not supported
        gadget->sendCheckCondition();
        return;
    }

    int length = 0;

    // Define our response header
    ModeSense10Header reply_header;
    memset(&reply_header, 0, sizeof(reply_header));
    reply_header.mediumType = CDUtils::GetMediumType(gadget);
    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense10", "Medium Type: 0x%02x", reply_header.mediumType);
    length += sizeof(reply_header);
    memcpy(gadget->m_InBuffer, &reply_header, sizeof(reply_header));

    // Process pages
    if (page == 0x3f) // All pages
    {
        CDROM_DEBUG_LOG("SCSIInquiry::ModeSense10", "Mode Sense All Mode Pages");
        // Original MODE SENSE (10) order: 01, 05, 08, 0D, 1a, 1c, 2a, 2D, 0e, 30, 31
        u8 pages[] = {0x01, 0x05, 0x08, 0x0D, 0x1a, 0x1c, 0x2a, 0x2D, 0x0e, 0x30, 0x31, 0x4e};
        for (u8 p : pages)
            FillModePage(gadget, p, gadget->m_InBuffer, length);
    }
    else
    {
        FillModePage(gadget, page, gadget->m_InBuffer, length);
    }

    // If unsupported page was requested (length didn't increase)
    if (length == sizeof(ModeSense10Header))
    {
        if (page != 0x3f)
        {
            gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN COMMAND PACKET
            gadget->sendCheckCondition();
            return;
        }
    }

    // Update header with data length
    ModeSense10Header *reply_header_ptr = (ModeSense10Header *)gadget->m_InBuffer;
    reply_header_ptr->modeDataLength = htons(length - 2);

    // Trim the reply length according to what the host requested
    if (allocationLength < length)
        length = allocationLength;

    CDROM_DEBUG_LOG("SCSIInquiry::ModeSense10", "Mode Sense (%d), Sending response with length %d", cdbSize, length);

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIInquiry::GetConfiguration(CUSBCDGadget *gadget)
{
    int rt = gadget->m_CBW.CBWCB[1] & 0x03;
    int startFeature = (gadget->m_CBW.CBWCB[2] << 8) | gadget->m_CBW.CBWCB[3];
    u16 allocationLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);

    int dataLength = 0;
    dataLength += sizeof(gadget->header);

    // RT=2 means return ONLY the requested feature (single feature query)
    if (rt == 0x02)
    {
        if (startFeature == 0x0002)
        {
            // Feature 0x0002: Morphing - Drive can report operational changes
            memcpy(gadget->m_InBuffer + dataLength, &gadget->morphing, sizeof(gadget->morphing));
            dataLength += sizeof(gadget->morphing);
        }
    }
    else
    {
        // RT!=2 means return ALL features starting from startFeature (full capability list)
        
        if (startFeature <= 0x0000)
        {
            // Feature 0x0000: Profile List - Describes all media types this drive can handle
            TUSBCDProfileListFeatureReply dynProfileList = gadget->profile_list;
            
            // CONDITIONAL: Only advertise DVD profile when DVD media is loaded
            if (gadget->m_mediaType == MEDIA_TYPE::DVD) {
                dynProfileList.AdditionalLength = 0x08;  // 2 profiles (CD + DVD)
            } else {
                dynProfileList.AdditionalLength = 0x04;  // 1 profile (CD only)
            }
            
            memcpy(gadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
            dataLength += sizeof(dynProfileList);

            // CONDITIONAL: Only include DVD profile descriptor if DVD media is present
            if (gadget->m_mediaType == MEDIA_TYPE::DVD) {
                TUSBCProfileDescriptorReply activeDVD = gadget->dvd_profile;
                activeDVD.currentP = 0x01;  // Mark as current profile
                memcpy(gadget->m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
                dataLength += sizeof(activeDVD);
            }

            // CD profile - always present (our base capability)
            TUSBCProfileDescriptorReply activeCD = gadget->cdrom_profile;
            activeCD.currentP = (gadget->m_mediaType != MEDIA_TYPE::DVD) ? 0x01 : 0x00;
            memcpy(gadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
            dataLength += sizeof(activeCD);
        }

        if (startFeature <= 0x0001)
        {
            // Feature 0x0001: Core - Basic SCSI multimedia command support
            memcpy(gadget->m_InBuffer + dataLength, &gadget->core, sizeof(gadget->core));
            dataLength += sizeof(gadget->core);
        }

        if (startFeature <= 0x0002)
        {
            // Feature 0x0002: Morphing - Asynchronous operational change notifications
            memcpy(gadget->m_InBuffer + dataLength, &gadget->morphing, sizeof(gadget->morphing));
            dataLength += sizeof(gadget->morphing);
        }

        if (startFeature <= 0x0003)
        {
            // Feature 0x0003: Removable Medium - Medium can be ejected/inserted
            memcpy(gadget->m_InBuffer + dataLength, &gadget->mechanism, sizeof(gadget->mechanism));
            dataLength += sizeof(gadget->mechanism);
        }

        if (startFeature <= 0x0010)
        {
            // Feature 0x0010: Random Readable - Can read any addressable block
            memcpy(gadget->m_InBuffer + dataLength, &gadget->randomreadable, sizeof(gadget->randomreadable));
            dataLength += sizeof(gadget->randomreadable);
        }

        if (startFeature <= 0x001D)
        {
            // Feature 0x001D: Multi-Read - Can read all CD media types
            memcpy(gadget->m_InBuffer + dataLength, &gadget->multiread, sizeof(gadget->multiread));
            dataLength += sizeof(gadget->multiread);
        }

        if (startFeature <= 0x001E)
        {
            // Feature 0x001E: CD Read - CD-specific structure reading (TOC, subcode, etc.)
            memcpy(gadget->m_InBuffer + dataLength, &gadget->cdread, sizeof(gadget->cdread));
            gadget->m_InBuffer[dataLength + 5] &= ~0x01;  // Clear DAP bit
            dataLength += sizeof(gadget->cdread);
        }

        // CONDITIONAL: Only advertise DVD Read feature when DVD media is loaded
        if (startFeature <= 0x001F && gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            // Feature 0x001F: DVD Read - DVD-specific structure reading
            memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdread, sizeof(gadget->dvdread));
            dataLength += sizeof(gadget->dvdread);
        }

        if (startFeature <= 0x0100)
        {
            // Feature 0x0100: Power Management - Drive can enter low-power states
            memcpy(gadget->m_InBuffer + dataLength, &gadget->powermanagement, sizeof(gadget->powermanagement));
            dataLength += sizeof(gadget->powermanagement);
        }

        if (startFeature <= 0x0103)
        {
            // Feature 0x0103: CD Audio External Play - Analog audio output support
            memcpy(gadget->m_InBuffer + dataLength, &gadget->audioplay, sizeof(gadget->audioplay));
            dataLength += sizeof(gadget->audioplay);
        }

        // CONDITIONAL: Only advertise DVD CSS when DVD media is loaded
        if (startFeature <= 0x0106 && gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            // Feature 0x0106: DVD CSS - Content Scramble System copy protection
            memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdcss, sizeof(gadget->dvdcss));
            dataLength += sizeof(gadget->dvdcss);
        }

        if (startFeature <= 0x0107)
        {
            // Feature 0x0107: Real Time Streaming - Can maintain real-time data streams
            memcpy(gadget->m_InBuffer + dataLength, &gadget->rtstreaming, sizeof(gadget->rtstreaming));
            dataLength += sizeof(gadget->rtstreaming);
        }
    }

    // Build the feature header with current profile and total data length
    TUSBCDFeatureHeaderReply dynHeader = gadget->header;

    if (gadget->m_mediaType == MEDIA_TYPE::DVD)
    {
        dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
        CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION: Returning PROFILE_DVD_ROM (0x0010)");
    }
    else
    {
        dynHeader.currentProfile = htons(PROFILE_CDROM); 
        CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION: Returning PROFILE_CDROM (0x0008)");
    }

    dynHeader.dataLength = htonl(dataLength - 4);
    memcpy(gadget->m_InBuffer, &dynHeader, sizeof(dynHeader));

    // Truncate response if host requested less data than we prepared
    if (allocationLength < dataLength)
        dataLength = allocationLength;

    gadget->m_nnumber_blocks = 0;
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, dataLength);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIInquiry::ModeSelect10(CUSBCDGadget *gadget)
{
    u16 transferLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);
    CDROM_DEBUG_LOG("SCSIInquiry::ModeSelect10", "Mode Select (10), transferLength is %u", transferLength);

    // Read the data from the host but don't do anything with it (yet!)
    gadget->m_nState = CUSBCDGadget::TCDState::DataOut;
    gadget->m_pEP[CUSBCDGadget::EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataOut,
                                                      gadget->m_OutBuffer, transferLength);

    // Unfortunately the payload doesn't arrive here. Check out the
    // ProcessOut method for payload processing

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
}
