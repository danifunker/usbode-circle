//
// scsi_toolbox.cpp
//
// SCSI Toolbox Commands
//
#include <usbcdgadget/scsi_toolbox.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
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

    gadget->m_pEPIn->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
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

    gadget->m_pEPIn->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
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

    memcpy(gadget->m_InBuffer, entries, count * sizeof(TUSBCDToolboxFileEntry));

    gadget->m_pEPIn->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
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
