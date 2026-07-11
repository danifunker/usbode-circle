#include <cassert>
#include <cstdio>
#include <cstring>

#include <circle/timer.h>
#include <tracelab/traceformat.h>
#include <tracelab/traceringbuffer.h>

static void test_header_sizes()
{
    // These must match the wire format the decoder (tools/usbode-trace)
    // parses; a mismatch here means the .h struct layout changed without
    // updating the decoder's struct format strings.
    assert(sizeof(TraceFileHeader) == 52);
    assert(sizeof(TraceRecordHeader) == 8);
    assert(sizeof(TraceSCSICDBPayload) == 18);
    assert(sizeof(TraceSCSICompletePayload) == 6);
    assert(sizeof(TraceSCSISensePayload) == 3);
}

static void test_basic_write_and_read()
{
    CTraceRingBuffer buf;
    assert(buf.Initialize(1024));

    TraceSCSISensePayload payload{0x06, 0x28, 0x00};
    CTimer::AdvanceForTest(100);
    assert(buf.WriteRecord(SCSI_SENSE_SET, &payload, sizeof(payload)));

    assert(buf.GetRecordCount() == 1);
    assert(buf.GetDroppedRecordCount() == 0);

    buf.ResetReadCursor();
    TraceRecordHeader header;
    u8 out[64];
    assert(buf.ReadNextRecord(&header, out, sizeof(out)));
    assert(header.eventType == SCSI_SENSE_SET);
    assert(header.payloadLength == sizeof(payload));
    assert(header.deltaTicks == 100);

    TraceSCSISensePayload *pOut = reinterpret_cast<TraceSCSISensePayload *>(out);
    assert(pOut->senseKey == 0x06);
    assert(pOut->asc == 0x28);
    assert(pOut->ascq == 0x00);

    // No more records.
    assert(!buf.ReadNextRecord(&header, out, sizeof(out)));
}

static void test_drop_when_full()
{
    // Buffer sized to fit exactly one small record.
    TraceSCSISensePayload payload{1, 2, 3};
    u32 nRecordSize = sizeof(TraceRecordHeader) + sizeof(payload);

    CTraceRingBuffer buf;
    assert(buf.Initialize(nRecordSize));

    assert(buf.WriteRecord(SCSI_SENSE_SET, &payload, sizeof(payload)));
    assert(buf.GetRecordCount() == 1);
    assert(buf.GetDroppedRecordCount() == 0);

    // Buffer is now full; the next write must be dropped, not overwrite.
    assert(!buf.WriteRecord(SCSI_SENSE_SET, &payload, sizeof(payload)));
    assert(buf.GetRecordCount() == 1);
    assert(buf.GetDroppedRecordCount() == 1);

    // The original record must still be intact.
    buf.ResetReadCursor();
    TraceRecordHeader header;
    u8 out[64];
    assert(buf.ReadNextRecord(&header, out, sizeof(out)));
    assert(header.payloadLength == sizeof(payload));
}

static void test_oversized_record_dropped()
{
    CTraceRingBuffer buf;
    assert(buf.Initialize(16));

    u8 tooBig[64] = {0};
    assert(!buf.WriteRecord(SCSI_CDB_RECEIVED, tooBig, sizeof(tooBig)));
    assert(buf.GetRecordCount() == 0);
    assert(buf.GetDroppedRecordCount() == 1);
}

int main()
{
    test_header_sizes();
    test_basic_write_and_read();
    test_drop_when_full();
    test_oversized_record_dropped();

    printf("All tracelab tests passed.\n");
    return 0;
}
