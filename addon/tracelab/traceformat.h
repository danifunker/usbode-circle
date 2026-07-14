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

// Event type ranges (see proposal for full layout):
//   0x0000  trace control       0x0100  USB bus/device state
//   0x0300  Bulk-Only Transport 0x0400  SCSI
//   0x0500  image access
enum TTraceEventType : u16
{
    TRACE_CAPTURE_START = 0x0001,
    TRACE_CAPTURE_STOP = 0x0002,   // no payload
    TRACE_TRIGGER_FIRED = 0x0005,  // no payload; error trigger matched

    // USB bus/device state (rare; recorded in standard and deep mode)
    USB_SUSPEND = 0x0101,          // no payload
    USB_ACTIVATE = 0x0102,         // no payload; device (re)configured
    USB_SPEED_NEGOTIATED = 0x0103, // TraceUSBSpeedPayload

    // Bulk-Only Transport data phase (deep mode only; per-command volume)
    BOT_IN_TRANSFER_START = 0x0300,    // TraceTransferPayload
    BOT_IN_TRANSFER_COMPLETE = 0x0301, // TraceTransferPayload

    SCSI_CDB_RECEIVED = 0x0400,
    SCSI_COMMAND_COMPLETE = 0x0401,
    SCSI_SENSE_SET = 0x0402,

    // Image/storage access (start/complete deep mode only; error always)
    IMAGE_READ_START = 0x0500,    // TraceImageReadPayload
    IMAGE_READ_COMPLETE = 0x0501, // TraceImageReadPayload (bytes actually read)
    IMAGE_READ_ERROR = 0x0502,    // TraceImageReadPayload
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

// Payload for USB_SPEED_NEGOTIATED
struct TraceUSBSpeedPayload
{
    u8 fullSpeed; // 1 = USB 1.1 full speed, 0 = USB 2.0 high speed
} PACKED;

// Payload for BOT_IN_TRANSFER_START / BOT_IN_TRANSFER_COMPLETE
struct TraceTransferPayload
{
    u32 bytes;
} PACKED;

// Payload for IMAGE_READ_START / IMAGE_READ_COMPLETE / IMAGE_READ_ERROR
struct TraceImageReadPayload
{
    u32 lba;
    u32 bytes; // requested (START/ERROR) or actually read (COMPLETE)
} PACKED;

#endif
