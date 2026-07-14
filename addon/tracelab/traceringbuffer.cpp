//
// traceringbuffer.cpp
//
#include <tracelab/traceringbuffer.h>

#include <circle/synchronize.h>
#include <circle/timer.h>
#include <circle/util.h>

CTraceRingBuffer::CTraceRingBuffer()
    : m_pBuffer(nullptr),
      m_nCapacity(0),
      m_nWriteOffset(0),
      m_nRecordCount(0),
      m_nDroppedRecordCount(0),
      m_nLastTimestamp(0),
      m_nReadOffset(0),
      m_nReadRemaining(0)
{
}

CTraceRingBuffer::~CTraceRingBuffer()
{
    delete[] m_pBuffer;
    m_pBuffer = nullptr;
}

boolean CTraceRingBuffer::Initialize(u32 nSizeBytes)
{
    if (m_pBuffer != nullptr || nSizeBytes == 0)
    {
        return FALSE;
    }

    m_pBuffer = new u8[nSizeBytes];
    if (m_pBuffer == nullptr)
    {
        return FALSE;
    }

    m_nCapacity = nSizeBytes;
    m_nWriteOffset = 0;
    m_nRecordCount = 0;
    m_nDroppedRecordCount = 0;
    m_nLastTimestamp = CTimer::GetClockTicks();

    return TRUE;
}

boolean CTraceRingBuffer::WriteRecord(u16 eventType, const void *pPayload, u16 nPayloadLength)
{
    if (m_pBuffer == nullptr)
    {
        return FALSE;
    }

    u32 nRecordSize = sizeof(TraceRecordHeader) + nPayloadLength;
    if (nRecordSize > m_nCapacity)
    {
        // Cannot ever fit; drop and report.
        m_nDroppedRecordCount++;
        return FALSE;
    }

    u64 nNow = CTimer::GetClockTicks();
    u64 nDelta = nNow - m_nLastTimestamp;

    TraceRecordHeader header;
    header.deltaTicks = (u32)nDelta;
    header.eventType = eventType;
    header.payloadLength = nPayloadLength;

    // Short critical section: reserve space and copy the fixed-size header
    // and small payload directly. No formatting, no allocation, no
    // filesystem/network access, no blocking.
    //
    // Phase 1 supports only manual/bounded capture (not the continuous
    // flight-recorder trigger mode from the proposal), so once the buffer
    // fills we stop accepting new records instead of overwriting old ones.
    // This keeps export a simple linear read and avoids needing to
    // reorder records around a wrap point.
    EnterCritical();

    if (m_nWriteOffset + nRecordSize > m_nCapacity)
    {
        LeaveCritical();
        m_nDroppedRecordCount++;
        return FALSE;
    }

    m_nLastTimestamp = nNow;

    memcpy(m_pBuffer + m_nWriteOffset, &header, sizeof(TraceRecordHeader));
    if (nPayloadLength > 0)
    {
        memcpy(m_pBuffer + m_nWriteOffset + sizeof(TraceRecordHeader), pPayload, nPayloadLength);
    }

    m_nWriteOffset += nRecordSize;
    m_nRecordCount++;

    LeaveCritical();

    return TRUE;
}

void CTraceRingBuffer::Reset()
{
    EnterCritical();

    m_nWriteOffset = 0;
    m_nRecordCount = 0;
    m_nDroppedRecordCount = 0;
    m_nLastTimestamp = CTimer::GetClockTicks();

    LeaveCritical();
}

void CTraceRingBuffer::ResetReadCursor()
{
    m_nReadOffset = 0;
    m_nReadRemaining = m_nWriteOffset;
}

boolean CTraceRingBuffer::ReadNextRecord(TraceRecordHeader *pHeaderOut, u8 *pPayloadOut, u16 nPayloadOutCapacity)
{
    if (m_pBuffer == nullptr || pHeaderOut == nullptr)
    {
        return FALSE;
    }

    if (m_nReadRemaining < sizeof(TraceRecordHeader))
    {
        return FALSE;
    }

    memcpy(pHeaderOut, m_pBuffer + m_nReadOffset, sizeof(TraceRecordHeader));

    u32 nRecordSize = sizeof(TraceRecordHeader) + pHeaderOut->payloadLength;
    if (nRecordSize > m_nReadRemaining)
    {
        // Truncated trailing record (shouldn't normally happen since we
        // only append complete records); stop reading.
        return FALSE;
    }

    if (pHeaderOut->payloadLength > 0 && pPayloadOut != nullptr)
    {
        u16 nCopyLength = pHeaderOut->payloadLength;
        if (nCopyLength > nPayloadOutCapacity)
        {
            nCopyLength = nPayloadOutCapacity;
        }
        memcpy(pPayloadOut, m_pBuffer + m_nReadOffset + sizeof(TraceRecordHeader), nCopyLength);
    }

    m_nReadOffset += nRecordSize;
    m_nReadRemaining -= nRecordSize;

    return TRUE;
}
