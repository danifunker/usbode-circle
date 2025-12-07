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
    u8 blocks = (u8)(gadget->m_CBW.CBWCB[4]);

    CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                    "REQUEST SENSE: mediaState=%d, sense=%02x/%02x/%02x -> reporting to host",
                    (int)gadget->m_mediaState,
                    gadget->m_SenseParams.bSenseKey, gadget->m_SenseParams.bAddlSenseCode, gadget->m_SenseParams.bAddlSenseCodeQual);

    u8 length = sizeof(TUSBCDRequestSenseReply);
    if (blocks < length)
        length = blocks;

    gadget->m_ReqSenseReply.bSenseKey = gadget->m_SenseParams.bSenseKey;
    gadget->m_ReqSenseReply.bAddlSenseCode = gadget->m_SenseParams.bAddlSenseCode;
    gadget->m_ReqSenseReply.bAddlSenseCodeQual = gadget->m_SenseParams.bAddlSenseCodeQual;

    memcpy(gadget->m_InBuffer, &gadget->m_ReqSenseReply, length);

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                     gadget->m_InBuffer, length);

    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->m_nState = CUSBCDGadget::TCDState::SendReqSenseReply;

    if (gadget->m_mediaState == CUSBCDGadget::MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        gadget->clearSenseData();
        gadget->m_mediaState = CUSBCDGadget::MediaState::MEDIUM_PRESENT_READY;
        gadget->bmCSWStatus = CD_CSW_STATUS_OK;
        CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                        "REQUEST SENSE: State transition UNIT_ATTENTION -> READY, sense cleared");
    }
    else if (gadget->m_mediaState == CUSBCDGadget::MediaState::NO_MEDIUM)
    {
        CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                        "REQUEST SENSE: NO_MEDIUM state - NOT clearing sense, keeping 02/3a/00");
    }
    else
    {
        gadget->clearSenseData();
        CDROM_DEBUG_LOG("SCSIInquiry::RequestSense",
                        "REQUEST SENSE: Clearing sense data");
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

        // Define our Code Page
        ModePage0x01Data codepage;
        memset(&codepage, 0, sizeof(codepage));

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

        if (strcmp(gadget->m_USBTargetOS, "apple") == 0)
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
            codepage.pageLength = 0x0E;

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
    int feature = (gadget->m_CBW.CBWCB[2] << 8) | gadget->m_CBW.CBWCB[3];
    u16 allocationLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);

    int dataLength = 0;

    switch (rt)
    {
    case 0x00: // All features supported
    case 0x01: // All current features supported
    {
        // offset to make space for the header
        dataLength += sizeof(gadget->header);

        // Dynamic profile list based on media type
        TUSBCDProfileListFeatureReply dynProfileList = gadget->profile_list;

        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            // Combo drive: advertise both profiles (8 bytes)
            dynProfileList.AdditionalLength = 0x08;
            memcpy(gadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
            dataLength += sizeof(dynProfileList);

            // MMC spec: descending order (DVD 0x0010 before CD 0x0008)
            TUSBCProfileDescriptorReply activeDVD = gadget->dvd_profile;
            activeDVD.currentP = 0x01; // DVD IS current
            memcpy(gadget->m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
            dataLength += sizeof(activeDVD);

            TUSBCProfileDescriptorReply activeCD = gadget->cdrom_profile;
            activeCD.currentP = 0x00; // CD not current
            memcpy(gadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
            dataLength += sizeof(activeCD);

            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION: DVD/CD combo drive, DVD current");
        }
        else
        {
            // CD-only drive: advertise only CD-ROM profile (4 bytes)
            dynProfileList.AdditionalLength = 0x04;
            memcpy(gadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
            dataLength += sizeof(dynProfileList);

            TUSBCProfileDescriptorReply activeCD = gadget->cdrom_profile;
            activeCD.currentP = 0x01; // CD IS current
            memcpy(gadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
            dataLength += sizeof(activeCD);

            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION: CD-ROM only drive");
        }

        memcpy(gadget->m_InBuffer + dataLength, &gadget->core, sizeof(gadget->core));
        dataLength += sizeof(gadget->core);

        memcpy(gadget->m_InBuffer + dataLength, &gadget->morphing, sizeof(gadget->morphing));
        dataLength += sizeof(gadget->morphing);

        memcpy(gadget->m_InBuffer + dataLength, &gadget->mechanism, sizeof(gadget->mechanism));
        dataLength += sizeof(gadget->mechanism);

        memcpy(gadget->m_InBuffer + dataLength, &gadget->randomreadable, sizeof(gadget->randomreadable));
        dataLength += sizeof(gadget->randomreadable);

        memcpy(gadget->m_InBuffer + dataLength, &gadget->multiread, sizeof(gadget->multiread));
        dataLength += sizeof(gadget->multiread);

        // For DVD media, add DVD Read feature instead of/in addition to CD Read
        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdread, sizeof(gadget->dvdread));
            dataLength += sizeof(gadget->dvdread);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x%02x): Sending DVD-Read feature (0x001f)", rt);
        }
        else
        {
            memcpy(gadget->m_InBuffer + dataLength, &gadget->cdread, sizeof(gadget->cdread));
            dataLength += sizeof(gadget->cdread);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x%02x): Sending CD-Read feature (0x001e), mediaType=%d", rt, (int)gadget->m_mediaType);
        }

        memcpy(gadget->m_InBuffer + dataLength, &gadget->powermanagement, sizeof(gadget->powermanagement));
        dataLength += sizeof(gadget->powermanagement);

        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdcss, sizeof(gadget->dvdcss));
            dataLength += sizeof(gadget->dvdcss);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x%02x): Sending DVD CSS feature (0x0106)", rt);
        }

        memcpy(gadget->m_InBuffer + dataLength, &gadget->audioplay, sizeof(gadget->audioplay));
        dataLength += sizeof(gadget->audioplay);

        memcpy(gadget->m_InBuffer + dataLength, &gadget->rtstreaming, sizeof(gadget->rtstreaming));
        dataLength += sizeof(gadget->rtstreaming);

        // Set header profile and copy to buffer
        TUSBCDFeatureHeaderReply dynHeader = gadget->header;
        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_DVD_ROM (0x0010)", rt);
        }
        else
        {
            dynHeader.currentProfile = htons(PROFILE_CDROM);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_CDROM (0x0008)", rt);
        }
        dynHeader.dataLength = htonl(dataLength - 4);
        memcpy(gadget->m_InBuffer, &dynHeader, sizeof(dynHeader));

        break;
    }

    case 0x02: // starting at the feature requested
    {
        // Offset for header
        dataLength += sizeof(gadget->header);

        switch (feature)
        {
        case 0x00:
        { // Profile list
            // Dynamic profile list: CD-only for CDs, combo for DVDs
            TUSBCDProfileListFeatureReply dynProfileList = gadget->profile_list;

            if (gadget->m_mediaType == MEDIA_TYPE::DVD)
            {
                // Combo drive: both profiles
                dynProfileList.AdditionalLength = 0x08;
                memcpy(gadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);

                TUSBCProfileDescriptorReply activeDVD = gadget->dvd_profile;
                activeDVD.currentP = 0x01;
                memcpy(gadget->m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
                dataLength += sizeof(activeDVD);

                TUSBCProfileDescriptorReply activeCD = gadget->cdrom_profile;
                activeCD.currentP = 0x00;
                memcpy(gadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);

                CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x00): DVD/CD combo, DVD current");
            }
            else
            {
                // CD-only drive: only CD profile
                dynProfileList.AdditionalLength = 0x04;
                memcpy(gadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);

                TUSBCProfileDescriptorReply activeCD = gadget->cdrom_profile;
                activeCD.currentP = 0x01;
                memcpy(gadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);

                CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x00): CD-ROM only drive (profile 0x0008, current=%d, length=0x%02x)",
                                activeCD.currentP, dynProfileList.AdditionalLength);
            }
            break;
        }

        case 0x01:
        { // Core
            memcpy(gadget->m_InBuffer + dataLength, &gadget->core, sizeof(gadget->core));
            dataLength += sizeof(gadget->core);
            break;
        }

        case 0x02:
        { // Morphing
            memcpy(gadget->m_InBuffer + dataLength, &gadget->morphing, sizeof(gadget->morphing));
            dataLength += sizeof(gadget->morphing);
            break;
        }

        case 0x03:
        { // Removable Medium
            memcpy(gadget->m_InBuffer + dataLength, &gadget->mechanism, sizeof(gadget->mechanism));
            dataLength += sizeof(gadget->mechanism);
            break;
        }

        case 0x10:
        { // Random Readable - CRITICAL for CD-ROM operation
            memcpy(gadget->m_InBuffer + dataLength, &gadget->randomreadable, sizeof(gadget->randomreadable));
            dataLength += sizeof(gadget->randomreadable);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x10): Sending Random Readable");
            break;
        }

        case 0x1d:
        { // Multiread
            memcpy(gadget->m_InBuffer + dataLength, &gadget->multiread, sizeof(gadget->multiread));
            dataLength += sizeof(gadget->multiread);
            break;
        }

        case 0x1e:
        { // CD-Read
            if (gadget->m_mediaType == MEDIA_TYPE::CD)
            {
                memcpy(gadget->m_InBuffer + dataLength, &gadget->cdread, sizeof(gadget->cdread));
                dataLength += sizeof(gadget->cdread);
            }
            break;
        }

        case 0x1f:
        { // DVD-Read
            if (gadget->m_mediaType == MEDIA_TYPE::DVD)
            {
                memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdread, sizeof(gadget->dvdread));
                dataLength += sizeof(gadget->dvdread);
            }
            break;
        }

        case 0x100:
        { // Power Management
            memcpy(gadget->m_InBuffer + dataLength, &gadget->powermanagement, sizeof(gadget->powermanagement));
            dataLength += sizeof(gadget->powermanagement);
            break;
        }

        case 0x103:
        { // Analogue Audio Play
            memcpy(gadget->m_InBuffer + dataLength, &gadget->audioplay, sizeof(gadget->audioplay));
            dataLength += sizeof(gadget->audioplay);
            break;
        }

        case 0x106:
        { // DVD CSS - Only return for DVD media
            if (gadget->m_mediaType == MEDIA_TYPE::DVD)
            {
                memcpy(gadget->m_InBuffer + dataLength, &gadget->dvdcss, sizeof(gadget->dvdcss));
                dataLength += sizeof(gadget->dvdcss);
                CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x106): Sending DVD CSS");
            }
            break;
        }

        case 0x107:
        { // Real Time Streaming - CRITICAL for CD-DA playback
            memcpy(gadget->m_InBuffer + dataLength, &gadget->rtstreaming, sizeof(gadget->rtstreaming));
            dataLength += sizeof(gadget->rtstreaming);
            // CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x107): Sending Real Time Streaming");
            break;
        }

        default:
        {
            // Log unhandled feature requests to identify what macOS is querying
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02): Unhandled feature 0x%04x requested", feature);
            break;
        }
        }

        // Set header profile and copy to buffer
        TUSBCDFeatureHeaderReply dynHeader = gadget->header;
        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02): Returning PROFILE_DVD_ROM (0x0010)");
        }
        else
        {
            dynHeader.currentProfile = htons(PROFILE_CDROM);
            CDROM_DEBUG_LOG("SCSIInquiry::GetConfiguration", "GET CONFIGURATION (rt 0x02): Returning PROFILE_CDROM (0x0008)");
        }
        dynHeader.dataLength = htonl(dataLength - 4);
        memcpy(gadget->m_InBuffer, &dynHeader, sizeof(dynHeader));
        break;
    }
    }

    // Set response length
    if (allocationLength < dataLength)
        dataLength = allocationLength;

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, dataLength);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    // gadget->m_CSW.bmCSWStatus = bmCSWStatus;
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
