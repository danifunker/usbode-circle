//
// test_read10.cpp
//
// READ(10) through the real DataInRead/Update() chunked-transfer path,
// including multi-chunk reads, USB 1.1 batch sizing, boundary clamping,
// and residue accounting.
//
#include "bench.h"
#include "framework.h"

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

static CGadgetTestBench::Result Read10(CGadgetTestBench &bench, u32 lba, u16 blocks)
{
    const u8 cdb[10] = {0x28, 0x00,
                        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                        0x00, (u8)(blocks >> 8), (u8)blocks, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), (u32)blocks * 2048);
}

TEST(read10_single_chunk)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    auto r = Read10(bench, 2, 4);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.dataChunks, 1);
    auto expected = ExpectedSectors(2, 4);
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
}

TEST(read10_multi_chunk_high_speed)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // 64 blocks > the 32-block high-speed batch: two Update() rounds.
    auto r = Read10(bench, 0, 64);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.dataChunks, 2);
    auto expected = ExpectedSectors(0, 64);
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
}

TEST(read10_multi_chunk_full_speed)
{
    // USB 1.1 (UHCI/OHCI hosts, and the Win98 target) batches 16 blocks.
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc, true /* full speed */);
    bench.Activate();
    bench.RequestSense();

    auto r = Read10(bench, 0, 64);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.dataChunks, 4);
    auto expected = ExpectedSectors(0, 64);
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
}

TEST(read10_beyond_end_rejected)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    auto r = Read10(bench, 1300, 1);

    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK(r.stalledIn);
    CHECK_EQ(r.csw.dCSWDataResidue, 2048u);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);
    CHECK_EQ(sense.data[12], 0x21); // LBA OUT OF RANGE
}

TEST(read10_truncated_at_disc_end)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // 4 blocks requested, only 2 exist: transfer 2, report the shortfall
    // in the residue.
    auto r = Read10(bench, 1198, 4);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)2 * 2048);
    CHECK_EQ(r.csw.dCSWDataResidue, 2u * 2048);
    auto expected = ExpectedSectors(1198, 2);
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());
}
