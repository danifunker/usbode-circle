//
// traceformat.h
//
// USBODE Trace Lab - on-disk/on-wire trace format (.utrace)
//
// Phase 1 scope: file header, record header, and the four Phase 1 event
// types (TRACE_CAPTURE_START, SCSI_CDB_RECEIVED, SCSI_COMMAND_COMPLETE,
// SCSI_SENSE_SET). See USBODE_Trace_Lab_Implementation_Proposal.md for the
// full design and later-phase event ranges.
//
#ifndef _tracelab_traceformat_h
#define _tracelab_traceformat_h

#include <circle/macros.h>
#include <circle/types.h>

#define TRACE_FORMAT_VERSION 1
#define TRACE_MAGIC "USBOTRCE"
#define TRACE_MAGIC_LEN 8

struct TraceFileHeader
{
    char magic[TRACE_MAGIC_LEN]; // "USBOTRCE"
    u16 formatVersion;           // TRACE_FORMAT_VERSION
    u16 headerSize;              // sizeof(TraceFileHeader)
    u32 flags;                   // reserved, 0 in Phase 1
    u64 captureStartTime;        // CTimer::GetClockTicks() at TRACE_CAPTURE_START
    u32 timerFrequency;          // ticks per second (CLOCKHZ)
    u32 firmwareVersion;         // reserved, 0 in Phase 1
    u32 boardModel;              // reserved, 0 in Phase 1
    u32 bufferSize;              // ring buffer capacity in bytes
    u32 recordCount;             // number of records written to this file
    u32 droppedRecordCount;      // records overwritten before export
    u32 mountedImageHash;        // reserved, 0 in Phase 1
} PACKED;

// Event type ranges (see proposal for full layout; Phase 1 only uses the
// control and SCSI ranges below).
enum TTraceEventType : u16
{
    TRACE_CAPTURE_START = 0x0001,

    SCSI_CDB_RECEIVED = 0x0400,
    SCSI_COMMAND_COMPLETE = 0x0401,
    SCSI_SENSE_SET = 0x0402,
};

struct TraceRecordHeader
{
    u32 deltaTicks;     // elapsed ticks since previous record's timestamp
    u16 eventType;      // TTraceEventType
    u16 payloadLength;  // size in bytes of the payload that follows this header
} PACKED;

// Payload for SCSI_CDB_RECEIVED
struct TraceSCSICDBPayload
{
    u8 lun;
    u8 cdbLength;
    u8 cdb[16];
} PACKED;

// Payload for SCSI_COMMAND_COMPLETE
struct TraceSCSICompletePayload
{
    u8 opcode;
    u8 status; // CD_CSW_STATUS_OK / _FAIL / _PHASE_ERR
    u32 residue;
} PACKED;

// Payload for SCSI_SENSE_SET
struct TraceSCSISensePayload
{
    u8 senseKey;
    u8 asc;
    u8 ascq;
} PACKED;

#endif
