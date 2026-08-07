//
// scsi_toolbox.cpp
//
// SCSI Toolbox Commands
//
#include <usbcdgadget/scsi_toolbox.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/wificonfig.h>
#include <scsitbservice/scsitbservice.h>
#include <circle/new.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

void SCSIToolbox::ListDevices(CUSBCDGadget* gadget)
{
    CDROM_DEBUG_LOG("SCSIToolbox::ListDevices", "SCSITB List Devices");

    // First device is CDROM and the other are not implemented
    u8 devices[] = {0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    memcpy(gadget->m_InBuffer, devices, sizeof(devices));

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, sizeof(devices));
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIToolbox::NumberOfFiles(CUSBCDGadget* gadget)
{
    MLOGNOTE("SCSIToolbox::NumberOfFiles", "SCSITB Number of Files/CDs");

    SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));

    // SCSITB defines max entries as 100
    const size_t MAX_ENTRIES = 100;
    size_t count = scsitbservice->GetCount();
    if (count > MAX_ENTRIES)
        count = MAX_ENTRIES;

    u8 num = (u8)count;

    MLOGNOTE("SCSIToolbox::NumberOfFiles", "SCSITB Discovered %d Files/CDs", num);

    memcpy(gadget->m_InBuffer, &num, sizeof(num));

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, sizeof(num));
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSIToolbox::ListFiles(CUSBCDGadget* gadget)
{
    MLOGNOTE("SCSIToolbox::ListFiles", "SCSITB List Files/CDs");

    SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));

    // SCSITB defines max entries as 100
    const size_t MAX_ENTRIES = 100;
    size_t count = scsitbservice->GetCount();
    if (count > MAX_ENTRIES)
        count = MAX_ENTRIES;

    // Value-initialized: only the characters of each name and its terminator
    // are written below, so with plain new[] the padding between an entry's
    // NUL and its size field would be uninitialized heap going out on the
    // wire - up to roughly 2 KB across a full catalog, different every call.
    TUSBCDToolboxFileEntry *entries = new TUSBCDToolboxFileEntry[MAX_ENTRIES]();
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

    memcpy(gadget->m_InBuffer, entries, count * sizeof(TUSBCDToolboxFileEntry));

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               gadget->m_InBuffer, count * sizeof(TUSBCDToolboxFileEntry));
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    delete[] entries;
}

void SCSIToolbox::SetNextCD(CUSBCDGadget* gadget)
{
    int index = gadget->m_CBW.CBWCB[1];
    MLOGNOTE("SCSIToolbox::SetNextCD", "SET NEXT CD index %d", index);

    // TODO set bounds checking here and throw check condition if index is not valid
    // currently, it will silently ignore OOB indexes

    SCSITBService *scsitbservice = static_cast<SCSITBService *>(CScheduler::Get()->GetTask("scsitbservice"));
    scsitbservice->SetNextCD(index);

    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->SendCSW();
}

// TOOLBOX_SEND_FILE_PREP carries a 33-byte parameter list: up to 32 filename
// characters and the NUL the client is required to include.
static const u32 SendFilePrepLength = 33;

// TOOLBOX_SEND_FILE_END is documented as taking no data, but the DOS client
// declares four bytes of it. Those two lengths are the only ones accepted.
static const u32 SendFileEndLength = 4;

// Sized from what the host declares over BOT, not from the CDB's valid count:
// the client moves a whole block whatever that count says.
void SCSIToolbox::BeginSendFileDataOut(CUSBCDGadget *gadget, u32 nLength)
{
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->m_nState = CUSBCDGadget::TCDState::DataOut;
    gadget->m_pEP[CUSBCDGadget::EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataOut,
                                                      gadget->m_OutBuffer, nLength);
}

// Sets sense on failure but never sends the CSW: the two D5 forms send it
// from different places.
bool SCSIToolbox::FinishSendFile(CUSBCDGadget *gadget)
{
    if (!CWiFiConfigUpload::Get().RequestCommit())
    {
        MLOGERR("SCSIToolbox::SendFileEnd", "No staged data to commit");
        CWiFiConfigUpload::Get().Abort();
        gadget->setSenseData(0x05, 0x2c, 0x00); // COMMAND SEQUENCE ERROR
        return false;
    }

    MLOGNOTE("SCSIToolbox::SendFileEnd", "Wi-Fi configuration queued for commit");
    return true;
}

void SCSIToolbox::SendFilePrep(CUSBCDGadget *gadget)
{
    gadget->m_nnumber_blocks = 0; // never resume a pending read behind this

    if (CWiFiConfigUpload::Get().IsBusy())
    {
        MLOGERR("SCSIToolbox::SendFilePrep", "A commit is already in flight");
        gadget->setSenseData(0x02, 0x04, 0x01); // LU IN PROCESS OF BECOMING READY
        gadget->sendCheckCondition();
        return;
    }

    // A second PREP abandons whatever the first one staged, whether or not this
    // one turns out to name an acceptable destination.
    CWiFiConfigUpload::Get().Abort();

    u32 nLength = gadget->m_CBW.dCBWDataTransferLength;
    if ((gadget->m_CBW.bmCBWFlags & 0x80) || nLength == 0 || nLength > SendFilePrepLength)
    {
        MLOGERR("SCSIToolbox::SendFilePrep", "Bad parameter list length %u", nLength);
        gadget->setSenseData(0x05, 0x1a, 0x00); // PARAMETER LIST LENGTH ERROR
        gadget->sendCheckCondition();
        return;
    }

    BeginSendFileDataOut(gadget, nLength);
}

void SCSIToolbox::SendFile10(CUSBCDGadget *gadget)
{
    gadget->m_nnumber_blocks = 0;

    if (!CWiFiConfigUpload::Get().IsReceiving())
    {
        MLOGERR("SCSIToolbox::SendFile10", "No upload in progress");
        gadget->setSenseData(0x05, 0x2c, 0x00); // COMMAND SEQUENCE ERROR
        gadget->sendCheckCondition();
        return;
    }

    u32 nValidLength = ((u32)gadget->m_CBW.CBWCB[1] << 8) | gadget->m_CBW.CBWCB[2];
    u32 nBlockIndex = ((u32)gadget->m_CBW.CBWCB[3] << 16) |
                      ((u32)gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

    if (nValidLength == 0 || nValidLength > CWiFiConfigUpload::BlockSize ||
        nBlockIndex > (CWiFiConfigUpload::MaxConfigSize - 1) / CWiFiConfigUpload::BlockSize)
    {
        MLOGERR("SCSIToolbox::SendFile10", "Block %u length %u out of range",
                nBlockIndex, nValidLength);
        CWiFiConfigUpload::Get().Abort();
        gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        gadget->sendCheckCondition();
        return;
    }

    u32 nLength = gadget->m_CBW.dCBWDataTransferLength;
    if ((gadget->m_CBW.bmCBWFlags & 0x80) || nLength < nValidLength ||
        nLength > CUSBCDGadget::MaxOutMessageSize)
    {
        MLOGERR("SCSIToolbox::SendFile10", "Declared transfer length %u unusable", nLength);
        CWiFiConfigUpload::Get().Abort();
        gadget->setSenseData(0x05, 0x1a, 0x00);
        gadget->sendCheckCondition();
        return;
    }

    BeginSendFileDataOut(gadget, nLength);
}

void SCSIToolbox::SendFileEnd(CUSBCDGadget *gadget)
{
    gadget->m_nnumber_blocks = 0;

    if (!CWiFiConfigUpload::Get().IsReceiving())
    {
        MLOGERR("SCSIToolbox::SendFileEnd", "No upload in progress");
        gadget->setSenseData(0x05, 0x2c, 0x00);
        gadget->sendCheckCondition();
        return;
    }

    // BOT 6.2: the direction bit means nothing when no data is declared, which
    // is exactly the documented no-payload form of this command.
    u32 nLength = gadget->m_CBW.dCBWDataTransferLength;
    if ((nLength != 0 && nLength != SendFileEndLength) ||
        (nLength > 0 && (gadget->m_CBW.bmCBWFlags & 0x80)))
    {
        MLOGERR("SCSIToolbox::SendFileEnd", "Declared transfer length %u unusable", nLength);
        CWiFiConfigUpload::Get().Abort();
        gadget->setSenseData(0x05, 0x1a, 0x00);
        gadget->sendCheckCondition();
        return;
    }

    // The four bytes the DOS client sends carry nothing, but they still have to
    // be drained before the commit so the data phase completes.
    if (nLength > 0)
    {
        BeginSendFileDataOut(gadget, nLength);
        return;
    }

    if (FinishSendFile(gadget))
    {
        gadget->sendGoodStatus();
    }
    else
    {
        gadget->sendCheckCondition();
    }
}

void SCSIToolbox::ProcessSendFileOut(CUSBCDGadget *gadget, size_t nLength)
{
    CWiFiConfigUpload &upload = CWiFiConfigUpload::Get();

    switch (gadget->m_CBW.CBWCB[0])
    {
    case 0xD3:
    {
        if (!upload.Begin(gadget->m_OutBuffer, nLength))
        {
            // The rejected name is not logged: it is host-supplied bytes.
            MLOGERR("SCSIToolbox::SendFilePrep", "Destination refused (%u bytes)",
                    (unsigned)nLength);
            gadget->setSenseData(0x05, 0x26, 0x00); // INVALID FIELD IN PARAMETER LIST
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        }
        break;
    }

    case 0xD4:
    {
        u32 nValidLength = ((u32)gadget->m_CBW.CBWCB[1] << 8) | gadget->m_CBW.CBWCB[2];
        u32 nBlockIndex = ((u32)gadget->m_CBW.CBWCB[3] << 16) |
                          ((u32)gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

        // Only the declared bytes are real; the rest of the 512-byte transfer
        // is whatever the client happened to have in its buffer.
        if (nValidLength > nLength)
        {
            MLOGERR("SCSIToolbox::SendFile10", "Short data phase: %u of %u bytes",
                    (unsigned)nLength, nValidLength);
            upload.Abort();
            gadget->setSenseData(0x05, 0x1a, 0x00);
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
            break;
        }

        if (!upload.Stage(nBlockIndex, gadget->m_OutBuffer, nValidLength))
        {
            MLOGERR("SCSIToolbox::SendFile10", "Block %u rejected", nBlockIndex);
            upload.Abort();
            gadget->setSenseData(0x05, 0x24, 0x00);
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        }
        break;
    }

    case 0xD5:
    {
        if (!FinishSendFile(gadget))
        {
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        }
        break;
    }
    }
}

void SCSIToolbox::ResetSendFileState(void)
{
    CWiFiConfigUpload::Get().Abort();
}
