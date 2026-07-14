//
// traceringbuffer.h
//
// Fixed-size, preallocated RAM ring buffer for USBODE Trace Lab events.
// No heap allocation after Initialize(). Safe to call ReserveRecord()/
// CommitRecord() from task context; a short critical section guards the
// write index so concurrent producers don't corrupt it.
//
#ifndef _tracelab_traceringbuffer_h
#define _tracelab_traceringbuffer_h

#include <circle/types.h>
#include <tracelab/traceformat.h>

class CTraceRingBuffer
{
public:
    CTraceRingBuffer();
    ~CTraceRingBuffer();

    // Allocates the buffer once (nSizeBytes bytes). Not called again while
    // capture is active.
    boolean Initialize(u32 nSizeBytes);

    // Reserves space for a header + payload, writes both, and advances the
    // buffer. Phase 1 supports manual/bounded capture only: once the buffer
    // fills, further records are dropped (counted, not overwritten) rather
    // than wrapping over older data. Never blocks, never allocates, never
    // touches the filesystem.
    boolean WriteRecord(u16 eventType, const void *pPayload, u16 nPayloadLength);

    // Resets read position to the oldest surviving record and returns
    // records in write order via successive calls. Used only during export
    // (SaveToSD), never on the hot path.
    void ResetReadCursor();
    boolean ReadNextRecord(TraceRecordHeader *pHeaderOut, u8 *pPayloadOut, u16 nPayloadOutCapacity);

    // Discards all recorded data (keeps the allocation) so a new capture
    // starts from an empty buffer. Caller must ensure no writer is active.
    void Reset();

    u32 GetRecordCount() const { return m_nRecordCount; }
    u32 GetDroppedRecordCount() const { return m_nDroppedRecordCount; }
    u32 GetCapacity() const { return m_nCapacity; }
    u32 GetUsedBytes() const { return m_nWriteOffset; }

private:
    u8 *m_pBuffer;
    u32 m_nCapacity;
    u32 m_nWriteOffset;
    u32 m_nRecordCount;
    u32 m_nDroppedRecordCount;
    u64 m_nLastTimestamp;

    // read-side state, valid only between ResetReadCursor() and export
    // completion
    u32 m_nReadOffset;
    u32 m_nReadRemaining;
};

#endif
