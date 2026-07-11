//
// tracelab.cpp
//
#include <tracelab/tracelab.h>

#include <assert.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <configservice/configservice.h>
#include <fatfs/ff.h>

static const char FromTraceLab[] = "tracelab";

#define DEFAULT_TRACE_BUFFER_KB 128

CTraceLab *CTraceLab::s_pThis = nullptr;

CTraceLab::CTraceLab()
    : m_bEnabled(FALSE),
      m_bDeepMode(FALSE),
      m_nCaptureStartTime(0)
{
    assert(s_pThis == nullptr);
    s_pThis = this;
}

CTraceLab::~CTraceLab()
{
    s_pThis = nullptr;
}

boolean CTraceLab::Initialize()
{
    if (m_RingBuffer.GetCapacity() > 0)
    {
        // Already initialized (the gadget can be re-created on a mode
        // switch); keep the existing buffer and enable state.
        return TRUE;
    }

    ConfigService *pConfig = (ConfigService *)CScheduler::Get()->GetTask("configservice");
    if (pConfig == nullptr)
    {
        m_bEnabled = FALSE;
        return FALSE;
    }

    const char *pMode = pConfig->GetProperty("trace_mode", "off");
    // "standard" records SCSI-level and rare USB bus events; "deep" adds
    // per-command image-read and BOT transfer records to decompose READ
    // latency (roughly 3x the record volume).
    m_bDeepMode = (strcmp(pMode, "deep") == 0);
    m_bEnabled = (strcmp(pMode, "standard") == 0) || m_bDeepMode;

    // Compatibility alias: debug_cdrom=1 with no explicit trace_mode also
    // turns on standard tracing, so existing users get the new tracer
    // without editing config.txt.
    if (!m_bEnabled && pConfig->GetProperty("debug_cdrom", 0U) != 0)
    {
        m_bEnabled = TRUE;
    }

    if (!m_bEnabled)
    {
        return TRUE;
    }

    unsigned nBufferKB = pConfig->GetProperty("trace_buffer_kb", (unsigned)DEFAULT_TRACE_BUFFER_KB);
    if (nBufferKB == 0)
    {
        nBufferKB = DEFAULT_TRACE_BUFFER_KB;
    }

    if (!m_RingBuffer.Initialize(nBufferKB * 1024))
    {
        CLogger::Get()->Write(FromTraceLab, LogError, "Failed to allocate %u KB trace buffer", nBufferKB);
        m_bEnabled = FALSE;
        return FALSE;
    }

    m_nCaptureStartTime = CTimer::GetClockTicks();
    m_RingBuffer.WriteRecord(TRACE_CAPTURE_START, nullptr, 0);

    CLogger::Get()->Write(FromTraceLab, LogNotice, "Trace Lab enabled (%u KB buffer, %s mode)",
                          nBufferKB, m_bDeepMode ? "deep" : "standard");

    return TRUE;
}

void CTraceLab::TraceCDBReceived(u8 lun, const u8 *pCDB, u8 nCDBLength)
{
    if (!m_bEnabled)
    {
        return;
    }

    TraceSCSICDBPayload payload;
    payload.lun = lun;
    payload.cdbLength = nCDBLength;
    memset(payload.cdb, 0, sizeof(payload.cdb));
    if (nCDBLength > sizeof(payload.cdb))
    {
        nCDBLength = sizeof(payload.cdb);
    }
    if (pCDB != nullptr && nCDBLength > 0)
    {
        memcpy(payload.cdb, pCDB, nCDBLength);
    }

    m_RingBuffer.WriteRecord(SCSI_CDB_RECEIVED, &payload, sizeof(payload));
}

void CTraceLab::TraceCommandComplete(u8 opcode, u8 status, u32 residue)
{
    if (!m_bEnabled)
    {
        return;
    }

    TraceSCSICompletePayload payload;
    payload.opcode = opcode;
    payload.status = status;
    payload.residue = residue;

    m_RingBuffer.WriteRecord(SCSI_COMMAND_COMPLETE, &payload, sizeof(payload));
}

void CTraceLab::TraceSenseSet(u8 senseKey, u8 asc, u8 ascq)
{
    if (!m_bEnabled)
    {
        return;
    }

    TraceSCSISensePayload payload;
    payload.senseKey = senseKey;
    payload.asc = asc;
    payload.ascq = ascq;

    m_RingBuffer.WriteRecord(SCSI_SENSE_SET, &payload, sizeof(payload));
}

void CTraceLab::TraceUSBSuspend()
{
    if (!m_bEnabled)
    {
        return;
    }
    m_RingBuffer.WriteRecord(USB_SUSPEND, nullptr, 0);
}

void CTraceLab::TraceUSBActivate()
{
    if (!m_bEnabled)
    {
        return;
    }
    m_RingBuffer.WriteRecord(USB_ACTIVATE, nullptr, 0);
}

void CTraceLab::TraceUSBSpeed(boolean bFullSpeed)
{
    if (!m_bEnabled)
    {
        return;
    }

    TraceUSBSpeedPayload payload;
    payload.fullSpeed = bFullSpeed ? 1 : 0;
    m_RingBuffer.WriteRecord(USB_SPEED_NEGOTIATED, &payload, sizeof(payload));
}

void CTraceLab::TraceImageReadStart(u32 lba, u32 bytes)
{
    if (!m_bDeepMode)
    {
        return;
    }

    TraceImageReadPayload payload;
    payload.lba = lba;
    payload.bytes = bytes;
    m_RingBuffer.WriteRecord(IMAGE_READ_START, &payload, sizeof(payload));
}

void CTraceLab::TraceImageReadComplete(u32 lba, u32 bytesRead)
{
    if (!m_bDeepMode)
    {
        return;
    }

    TraceImageReadPayload payload;
    payload.lba = lba;
    payload.bytes = bytesRead;
    m_RingBuffer.WriteRecord(IMAGE_READ_COMPLETE, &payload, sizeof(payload));
}

void CTraceLab::TraceImageReadError(u32 lba, u32 bytes)
{
    if (!m_bEnabled)
    {
        return;
    }

    TraceImageReadPayload payload;
    payload.lba = lba;
    payload.bytes = bytes;
    m_RingBuffer.WriteRecord(IMAGE_READ_ERROR, &payload, sizeof(payload));
}

void CTraceLab::TraceTransferStart(u32 bytes)
{
    if (!m_bDeepMode)
    {
        return;
    }

    TraceTransferPayload payload;
    payload.bytes = bytes;
    m_RingBuffer.WriteRecord(BOT_IN_TRANSFER_START, &payload, sizeof(payload));
}

void CTraceLab::TraceTransferComplete(u32 bytes)
{
    if (!m_bDeepMode)
    {
        return;
    }

    TraceTransferPayload payload;
    payload.bytes = bytes;
    m_RingBuffer.WriteRecord(BOT_IN_TRANSFER_COMPLETE, &payload, sizeof(payload));
}

u32 CTraceLab::ExportToBuffer(u8 *pBuffer, u32 nMaxLength)
{
    if (!m_bEnabled || pBuffer == nullptr || nMaxLength < sizeof(TraceFileHeader))
    {
        return 0;
    }

    TraceFileHeader header;
    memcpy(header.magic, TRACE_MAGIC, TRACE_MAGIC_LEN);
    header.formatVersion = TRACE_FORMAT_VERSION;
    header.headerSize = sizeof(TraceFileHeader);
    header.flags = 0;
    header.captureStartTime = m_nCaptureStartTime;
    header.timerFrequency = CLOCKHZ;
    header.firmwareVersion = 0;
    header.boardModel = 0;
    header.bufferSize = m_RingBuffer.GetCapacity();
    header.recordCount = m_RingBuffer.GetRecordCount();
    header.droppedRecordCount = m_RingBuffer.GetDroppedRecordCount();
    header.mountedImageHash = 0;

    memcpy(pBuffer, &header, sizeof(header));
    u32 nOffset = sizeof(header);

    m_RingBuffer.ResetReadCursor();

    // Stop after the record count snapshotted into the header so the file
    // stays self-consistent even if capture continues during the export.
    TraceRecordHeader recordHeader;
    u8 payload[256];
    u32 nRecordsWritten = 0;
    while (nRecordsWritten < header.recordCount
           && m_RingBuffer.ReadNextRecord(&recordHeader, payload, sizeof(payload)))
    {
        u16 nPayloadLength = recordHeader.payloadLength;
        if (nPayloadLength > sizeof(payload))
        {
            nPayloadLength = sizeof(payload);
        }

        if (nOffset + sizeof(recordHeader) + nPayloadLength > nMaxLength)
        {
            CLogger::Get()->Write(FromTraceLab, LogError,
                                  "Trace capture does not fit in %u byte export buffer", nMaxLength);
            return 0;
        }

        memcpy(pBuffer + nOffset, &recordHeader, sizeof(recordHeader));
        nOffset += sizeof(recordHeader);
        if (nPayloadLength > 0)
        {
            memcpy(pBuffer + nOffset, payload, nPayloadLength);
            nOffset += nPayloadLength;
        }
        nRecordsWritten++;
    }

    return nOffset;
}

boolean CTraceLab::SaveToSD(const char *pFilePath)
{
    if (!m_bEnabled)
    {
        return FALSE;
    }

    FIL File;
    FRESULT Result = f_open(&File, pFilePath, FA_WRITE | FA_CREATE_ALWAYS);
    if (Result != FR_OK)
    {
        CLogger::Get()->Write(FromTraceLab, LogError, "Failed to open %s for trace export", pFilePath);
        return FALSE;
    }

    TraceFileHeader header;
    memcpy(header.magic, TRACE_MAGIC, TRACE_MAGIC_LEN);
    header.formatVersion = TRACE_FORMAT_VERSION;
    header.headerSize = sizeof(TraceFileHeader);
    header.flags = 0;
    header.captureStartTime = m_nCaptureStartTime;
    header.timerFrequency = CLOCKHZ;
    header.firmwareVersion = 0;
    header.boardModel = 0;
    header.bufferSize = m_RingBuffer.GetCapacity();
    header.recordCount = m_RingBuffer.GetRecordCount();
    header.droppedRecordCount = m_RingBuffer.GetDroppedRecordCount();
    header.mountedImageHash = 0;

    UINT nBytesWritten;
    Result = f_write(&File, &header, sizeof(header), &nBytesWritten);
    if (Result != FR_OK || nBytesWritten != sizeof(header))
    {
        CLogger::Get()->Write(FromTraceLab, LogError, "Failed to write trace header to %s", pFilePath);
        f_close(&File);
        return FALSE;
    }

    m_RingBuffer.ResetReadCursor();

    TraceRecordHeader recordHeader;
    u8 payload[256];
    while (m_RingBuffer.ReadNextRecord(&recordHeader, payload, sizeof(payload)))
    {
        Result = f_write(&File, &recordHeader, sizeof(recordHeader), &nBytesWritten);
        if (Result != FR_OK || nBytesWritten != sizeof(recordHeader))
        {
            break;
        }

        if (recordHeader.payloadLength > 0)
        {
            u16 nCopyLength = recordHeader.payloadLength;
            if (nCopyLength > sizeof(payload))
            {
                nCopyLength = sizeof(payload);
            }
            Result = f_write(&File, payload, nCopyLength, &nBytesWritten);
            if (Result != FR_OK || nBytesWritten != nCopyLength)
            {
                break;
            }
        }
    }

    f_sync(&File);
    f_close(&File);

    CLogger::Get()->Write(FromTraceLab, LogNotice, "Trace saved to %s (%u records, %u dropped)",
                          pFilePath, m_RingBuffer.GetRecordCount(), m_RingBuffer.GetDroppedRecordCount());

    return TRUE;
}
