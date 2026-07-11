//
// tracelab.h
//
// USBODE Trace Lab - compact binary event tracer, Phase 1.
//
// Replaces high-volume debug_cdrom text logging on timing-sensitive SCSI
// paths with fixed-size binary records appended to a preallocated RAM ring
// buffer (CTraceRingBuffer). No formatting, allocation, or filesystem
// access happens on the hot path; SaveToSD() exports the buffer to a
// .utrace file after capture stops.
//
// Phase 1 covers only: TRACE_CAPTURE_START, SCSI_CDB_RECEIVED,
// SCSI_COMMAND_COMPLETE, SCSI_SENSE_SET. See
// USBODE_Trace_Lab_Implementation_Proposal.md for later phases.
//
#ifndef _tracelab_tracelab_h
#define _tracelab_tracelab_h

#include <circle/types.h>
#include <tracelab/traceformat.h>
#include <tracelab/traceringbuffer.h>

class CTraceLab
{
public:
    CTraceLab();
    ~CTraceLab();

    static CTraceLab *Get() { return s_pThis; }

    // Reads trace_mode/trace_buffer_kb from ConfigService and allocates the
    // ring buffer if trace_mode=standard. Safe to call once at startup,
    // mirroring how CUSBCDGadget reads debug_cdrom in its constructor.
    boolean Initialize();

    boolean IsEnabled() const { return m_bEnabled; }
    boolean IsDeepMode() const { return m_bDeepMode; }

    u32 GetRecordCount() const { return m_RingBuffer.GetRecordCount(); }
    u32 GetDroppedRecordCount() const { return m_RingBuffer.GetDroppedRecordCount(); }
    u32 GetBufferCapacity() const { return m_RingBuffer.GetCapacity(); }

    // Hot-path trace calls. Each is a no-op (single branch) when disabled.
    void TraceCDBReceived(u8 lun, const u8 *pCDB, u8 nCDBLength);
    void TraceCommandComplete(u8 opcode, u8 status, u32 residue);
    void TraceSenseSet(u8 senseKey, u8 asc, u8 ascq);

    // Phase 2: USB bus/device state (rare events, recorded in any mode).
    void TraceUSBSuspend();
    void TraceUSBActivate();
    void TraceUSBSpeed(boolean bFullSpeed);

    // Phase 2: per-command detail, recorded only in trace_mode=deep so
    // standard mode keeps its low record volume. Start/complete pairs
    // decompose SCSI READ latency into storage time and USB wire time.
    void TraceImageReadStart(u32 lba, u32 bytes);
    void TraceImageReadComplete(u32 lba, u32 bytesRead);
    void TraceImageReadError(u32 lba, u32 bytes); // recorded in any mode
    void TraceTransferStart(u32 bytes);
    void TraceTransferComplete(u32 bytes);

    // Writes the current buffer contents to a .utrace file on the SD card.
    // Must only be called from task context (not the USB hot path).
    boolean SaveToSD(const char *pFilePath);

    // Serializes the same .utrace stream into a caller-provided buffer, for
    // HTTP download without touching the SD card. Returns bytes written, or
    // 0 if disabled or the capture does not fit in nMaxLength. Task context
    // only, like SaveToSD().
    u32 ExportToBuffer(u8 *pBuffer, u32 nMaxLength);

private:
    static CTraceLab *s_pThis;

    boolean m_bEnabled;
    boolean m_bDeepMode;
    CTraceRingBuffer m_RingBuffer;
    u64 m_nCaptureStartTime;
};

#endif
