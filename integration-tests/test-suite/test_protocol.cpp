//
// test_protocol.cpp
//
// Protocol-level and boundary handling: malformed CBWs, READ(12), exact
// disc-edge addressing, and zero-length allocation requests.
//
// These are the requests a drive sees from confused, buggy, or merely
// thorough hosts. They matter because the failure mode is asymmetric: a
// command answered wrongly usually produces a visible error, but a CBW
// mishandled at the transport layer wedges the endpoint, and the host's
// only way out is a bus reset -- which looks to the user like USBODE
// randomly disconnecting.
//
#include "bench.h"
#include "framework.h"

#include <string.h>

#include <vector>

static std::vector<u8> ExpectedSectors(u32 firstLBA, u32 count)
{
    std::vector<u8> expected((size_t)count * 2048);
    for (u32 i = 0; i < count; i++)
    {
        FillPatternSector(expected.data() + (size_t)i * 2048, firstLBA + i, 2048);
    }
    return expected;
}

static CGadgetTestBench::Result Read12(CGadgetTestBench &bench, u32 lba, u32 blocks)
{
    const u8 cdb[12] = {0xA8, 0x00,
                        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                        (u8)(blocks >> 24), (u8)(blocks >> 16), (u8)(blocks >> 8), (u8)blocks,
                        0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), blocks * 2048);
}

static CGadgetTestBench::Result Read10(CGadgetTestBench &bench, u32 lba, u16 blocks)
{
    const u8 cdb[10] = {0x28, 0x00,
                        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                        0x00, (u8)(blocks >> 8), (u8)blocks, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), (u32)blocks * 2048);
}

// ---------------------------------------------------------------------------
// Malformed CBWs (BOT 6.6.1: an invalid CBW must stall, not be executed)
// ---------------------------------------------------------------------------

// A CBW whose signature is not 'USBC' is not a CBW at all -- it usually means
// the host and device have lost framing. Executing CBWCB[0] out of a buffer
// like this would run an arbitrary opcode against the disc, so the gadget must
// reject it at the transport layer and stall instead.
TEST(cbw_invalid_signature_stalls)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();

    TUSBCDCBW cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = 0x55534243; // 'USBC' byte-swapped: a plausible near-miss
    cbw.dCBWTag = 0x1234;
    cbw.dCBWDataTransferLength = 2048;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 10;
    cbw.CBWCB[0] = 0x28; // a READ(10) that must NOT be executed

    auto r = bench.SendRawCBW(&cbw, SIZE_CBW);

    CHECK(r.stalledIn);
    CHECK(!r.gotCSW);              // no status for a CBW that was never valid
    CHECK_EQ(r.data.size(), (size_t)0); // and certainly no disc data
}

// A short packet where a 31-byte CBW was expected is also invalid. The check
// must happen before the memcpy into m_CBW, or the gadget acts on whatever
// stale bytes were left in the buffer by the previous command.
TEST(cbw_short_packet_stalls)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();

    TUSBCDCBW cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = VALID_CBW_SIG;
    cbw.dCBWTag = 0x1234;
    cbw.bCBWCBLength = 6;
    cbw.CBWCB[0] = 0x00; // TEST UNIT READY

    auto r = bench.SendRawCBW(&cbw, SIZE_CBW - 1); // one byte short

    CHECK(r.stalledIn);
    CHECK(!r.gotCSW);
}

// ---------------------------------------------------------------------------
// READ(12): the same read path reached through the 12-byte CDB
// ---------------------------------------------------------------------------

// READ(12) parses the block count from four bytes instead of two. A byte-order
// or offset slip in that parsing reads the wrong sectors -- or, with a count
// misread as huge, walks off the disc.
TEST(read12_reads_correct_sectors)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    auto r = Read12(bench, 7, 3);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    auto expected = ExpectedSectors(7, 3);
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
}

// The boundary check applies to READ(12) as well as READ(10).
TEST(read12_beyond_end_rejected)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    auto r = Read12(bench, 1200, 1); // first LBA past the leadout

    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK(r.stalledIn);
    CHECK_EQ(r.csw.dCSWDataResidue, 2048u);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);  // ILLEGAL REQUEST
    CHECK_EQ(sense.data[12], 0x21); // LOGICAL BLOCK ADDRESS OUT OF RANGE
}

// ---------------------------------------------------------------------------
// Exact disc edge
// ---------------------------------------------------------------------------

// The off-by-one that matters: the last addressable sector is leadout - 1 and
// must read; leadout itself must be rejected. An installer copying a disc
// reads right up to this edge, so a >= that should be > (or vice versa) shows
// up as a file copy failing at 99%.
TEST(read10_last_sector_is_addressable)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Last valid sector reads normally.
    {
        auto r = Read10(bench, 1199, 1);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK_EQ(r.csw.dCSWDataResidue, 0u);
        auto expected = ExpectedSectors(1199, 1);
        CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
    }
    // One past it does not.
    {
        auto r = Read10(bench, 1200, 1);
        CHECK_EQ(r.csw.bmCSWStatus, 1);
        auto sense = bench.RequestSense();
        CHECK_EQ(sense.data[2], 0x05);
        CHECK_EQ(sense.data[12], 0x21);
    }
}

// ---------------------------------------------------------------------------
// Zero-length allocation requests
// ---------------------------------------------------------------------------

// Allocation length 0 means "return no data" -- it is a legal request, not an
// error, and hosts do issue it (notably when sizing a response in two steps).
// The drive must answer GOOD with an empty data phase and residue 0, not
// CHECK CONDITION and not a short transfer the host will sit waiting on.
TEST(inquiry_zero_allocation_length)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[6] = {0x12, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.data.size(), (size_t)0);
}

// Same for READ TOC, which is where hosts most often probe with a small or
// zero allocation length before asking for the real thing.
TEST(read_toc_zero_allocation_length)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 400);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.data.size(), (size_t)0);
}
