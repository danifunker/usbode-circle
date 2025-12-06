//
// scsi_misc.cpp
//
// SCSI Miscellaneous Commands
//
#include <usbcdgadget/scsi_misc.h>
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

void SCSIMisc::TestUnitReady(CUSBCDGadget* gadget)
{
    CDROM_DEBUG_LOG("SCSIMisc::TestUnitReady",
                    "TEST UNIT READY: m_CDReady=%d, mediaState=%d, sense=%02x/%02x/%02x",
                    gadget->m_CDReady, (int)gadget->m_mediaState,
                    gadget->m_SenseParams.bSenseKey, gadget->m_SenseParams.bAddlSenseCode, gadget->m_SenseParams.bAddlSenseCodeQual);

    if (!gadget->m_CDReady)
    {
        CDROM_DEBUG_LOG("SCSIMisc::TestUnitReady", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        gadget->setSenseData(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT
        gadget->m_mediaState = CUSBCDGadget::MediaState::NO_MEDIUM;
        gadget->sendCheckCondition();
        return;
    }

    if (gadget->m_mediaState == CUSBCDGadget::MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        CDROM_DEBUG_LOG("SCSIMisc::TestUnitReady",
                        "TEST UNIT READY -> CHECK CONDITION (sense 06/28/00 - UNIT ATTENTION)");
        gadget->setSenseData(0x06, 0x28, 0x00); // UNIT ATTENTION - MEDIA CHANGED
        gadget->sendCheckCondition();
        CTimer::Get()->MsDelay(100);
        return;
    }

    CDROM_DEBUG_LOG("SCSIMisc::TestUnitReady",
                    "TEST UNIT READY -> GOOD STATUS");

    // CDROM_DEBUG_LOG ("SCSIMisc::TestUnitReady", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
    gadget->sendGoodStatus();
}

void SCSIMisc::StartStopUnit(CUSBCDGadget* gadget)
{
    int start = gadget->m_CBW.CBWCB[4] & 1;
    int loej = (gadget->m_CBW.CBWCB[4] >> 1) & 1;
    // TODO: Emulate a disk eject/load
    // loej Start Action
    // 0    0     Stop the disc - no action for us
    // 0    1     Start the disc - no action for us
    // 1    0     Eject the disc - perhaps we need to throw a check condition?
    // 1    1     Load the disc - perhaps we need to throw a check condition?

    CDROM_DEBUG_LOG("SCSIMisc::StartStopUnit", "start/stop, start = %d, loej = %d", start, loej);
    gadget->sendGoodStatus();
}

void SCSIMisc::PreventAllowMediumRemoval(CUSBCDGadget* gadget)
{
    // Lie to the host
    gadget->sendGoodStatus();
}

void SCSIMisc::ReadCapacity(CUSBCDGadget* gadget)
{
    gadget->m_ReadCapReply.nLastBlockAddr = htonl(CDUtils::GetLeadoutLBA(gadget) - 1); // this value is the Start address of last recorded lead-out minus 1
    memcpy(gadget->m_InBuffer, &gadget->m_ReadCapReply, SIZE_READCAPREP);
    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, SIZE_READCAPREP);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
}

void SCSIMisc::MechanismStatus(CUSBCDGadget* gadget)
{
    u16 allocationLength = (gadget->m_CBW.CBWCB[8] << 8) | gadget->m_CBW.CBWCB[9];

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

    memcpy(gadget->m_InBuffer, &status, length);
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIMisc::GetEventStatusNotification(CUSBCDGadget* gadget)
{
    u8 polled = gadget->m_CBW.CBWCB[1] & 0x01;
    u8 notificationClass = gadget->m_CBW.CBWCB[4]; // This is a bitmask
    u16 allocationLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);

    CDROM_DEBUG_LOG("SCSIMisc::GetEventStatusNotification", "Get Event Status Notification");

    if (polled == 0)
    {
        // We don't support async mode
        MLOGNOTE("SCSIMisc::GetEventStatusNotification", "Get Event Status Notification - we don't support async notifications");
        gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        gadget->sendCheckCondition();
        return;
    }

    int length = 0;
    // Event Header
    TUSBCDEventStatusReplyHeader header;
    memset(&header, 0, sizeof(header));
    header.supportedEventClass = 0x10; // Only support media change events (10000b)

    // Media Change Event Request
    if (notificationClass & (1 << 4))
    {

        MLOGNOTE("SCSIMisc::GetEventStatusNotification", "Get Event Status Notification - media change event response");

        // Update header
        header.eventDataLength = htons(0x04); // Always 4 because only return 1 event
        header.notificationClass = 0x04;      // 100b = media class

        // Define the event
        TUSBCDEventStatusReplyEvent event;
        memset(&event, 0, sizeof(event));

        if (gadget->discChanged)
        {
            MLOGNOTE("SCSIMisc::GetEventStatusNotification", "Get Event Status Notification - sending NewMedia event");
            event.eventCode = 0x02;                  // NewMedia event
            event.data[0] = gadget->m_CDReady ? 0x02 : 0x00; // Media present : No media

            // Only clear the disc changed event if we're actually going to send the full response
            if (allocationLength >= (sizeof(TUSBCDEventStatusReplyHeader) + sizeof(TUSBCDEventStatusReplyEvent)))
            {
                gadget->discChanged = false;
            }
        }
        else if (gadget->m_CDReady)
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
        memcpy(gadget->m_InBuffer + sizeof(TUSBCDEventStatusReplyHeader), &event, sizeof(TUSBCDEventStatusReplyEvent));
        length += sizeof(TUSBCDEventStatusReplyEvent);
    }
    else
    {
        // No supported event class requested
        MLOGNOTE("SCSIMisc::GetEventStatusNotification", "Get Event Status Notification - no supported class requested");
        header.notificationClass = 0x00;
        header.eventDataLength = htons(0x00);
    }

    memcpy(gadget->m_InBuffer, &header, sizeof(TUSBCDEventStatusReplyHeader));
    length += sizeof(TUSBCDEventStatusReplyHeader);

    if (allocationLength < length)
        length = allocationLength;

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIMisc::GetPerformance(CUSBCDGadget* gadget)
{
    CDROM_DEBUG_LOG("SCSIMisc::GetPerformance", "GET PERFORMANCE (0xAC)");

    u8 getPerformanceStub[20] = {
        0x00, 0x00, 0x00, 0x10, // Header: Length = 16 bytes (descriptor)
        0x00, 0x00, 0x00, 0x00, // Reserved or Start LBA
        0x00, 0x00, 0x00, 0x00, // Reserved or End LBA
        0x00, 0x00, 0x00, 0x01, // Performance metric (e.g. 1x speed)
        0x00, 0x00, 0x00, 0x00  // Additional reserved
    };

    memcpy(gadget->m_InBuffer, getPerformanceStub, sizeof(getPerformanceStub));

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, sizeof(getPerformanceStub));
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
}

void SCSIMisc::CommandA4(CUSBCDGadget* gadget)
{
    CDROM_DEBUG_LOG("SCSIMisc::CommandA4", "A4 from Win2k");

    // Response copied from an ASUS CDROM drive. It seems to know
    // what this is, so let's just copy it
    u8 response[] = {0x0, 0x6, 0x0, 0x0, 0x25, 0xff, 0x1, 0x0};

    memcpy(gadget->m_InBuffer, response, sizeof(response));

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, sizeof(response));
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIMisc::Verify(CUSBCDGadget* gadget)
{
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->SendCSW();
}

void SCSIMisc::SetCDROMSpeed(CUSBCDGadget* gadget)
{
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->SendCSW();
}
