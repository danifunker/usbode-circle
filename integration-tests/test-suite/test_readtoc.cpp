//
// test_readtoc.cpp
//
// READ TOC in every format Win9x and modern hosts use, including the two
// vendor/legacy CDB[9] encodings that shipped as Win98 fixes in 3.2.x.
//
#include "bench.h"
#include "framework.h"

TEST(read_toc_format0_lba)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 100, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 100);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    // 1 track + leadout: header(4) + 2 descriptors(8) = 20 bytes.
    const u8 expected[20] = {
        0x00, 0x12,             // TOC length = 18
        0x01, 0x01,             // first/last track
        0x00, 0x14, 0x01, 0x00, // track 1: data track, ADR 1
        0x00, 0x00, 0x00, 0x00, // LBA 0
        0x00, 0x14, 0xAA, 0x00, // leadout
        0x00, 0x00, 0x04, 0xB0, // LBA 1200
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));

    // Host asked for 100, got 20 -> residue 80. Win98's usbstor.sys
    // discards short responses whose CSW claims residue 0.
    CHECK_EQ(r.csw.dCSWDataResidue, 80u);
}

TEST(read_toc_format0_msf)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 100, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 100);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[20] = {
        0x00, 0x12,
        0x01, 0x01,
        0x00, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x02, 0x00, // LBA 0 -> MSF 00:02:00
        0x00, 0x14, 0xAA, 0x00,
        0x00, 0x00, 0x12, 0x00, // LBA 1200 -> MSF 00:18:00
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(read_toc_leadout_only)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Starting track 0xAA requests only the leadout descriptor.
    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x00, 100, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 100);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[12] = {
        0x00, 0x0A,
        0x01, 0x01,
        0x00, 0x14, 0xAA, 0x00,
        0x00, 0x00, 0x04, 0xB0,
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(read_toc_legacy_cdb9_session_info)
{
    // Win9x's CD-ROM class driver encodes "session info" in CDB[9] bits
    // 7-6 (old SFF-8020i/ATAPI style). Answering with a full TOC instead
    // broke CD audio ("data or no disc loaded").
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 12, 0x40};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 12);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[12] = {
        0x00, 0x0A, // length 10
        0x01, 0x01, // first/last session
        0x00, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, // first track of session at LBA 0
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
}

TEST(read_toc_matshita_bcd_full_toc)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // CDB[9] = 0x80: vendor extension, full TOC with BCD addresses.
    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 256);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    // A0/A1/A2 descriptors + 1 track descriptor = 37 + 11 = 48 bytes.
    CHECK_EQ(r.data.size(), (size_t)48);
    CHECK_EQ(r.data[12], 0x01); // A0: first track
    CHECK_EQ(r.data[23], 0x01); // A1: last track
    // A2: leadout LBA 1200 -> MSF 00:18:00 -> BCD 00 18 00
    CHECK_EQ(r.data[34], 0x00);
    CHECK_EQ(r.data[35], 0x18);
    CHECK_EQ(r.data[36], 0x00);
    // Track 1 descriptor: session 1, data control, POINT 01, start MSF
    // 00:02:00 in BCD.
    const u8 track1[11] = {0x01, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                           0x00, 0x02, 0x00};
    CHECK_BYTES(r.data.data() + 37, 11, track1, 11);
}

TEST(read_toc_allocation_truncation)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 4, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 4);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)4);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    const u8 expected[4] = {0x00, 0x12, 0x01, 0x01};
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(read_toc_audio_control_bits)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 100, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 100);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    // 3 tracks + leadout = 4 descriptors + header = 36 bytes.
    CHECK_EQ(r.data.size(), (size_t)36);
    CHECK_EQ(r.data[2], 0x01);
    CHECK_EQ(r.data[3], 0x03);
    CHECK_EQ(r.data[5], 0x10);  // track 1 control: audio
    CHECK_EQ(r.data[13], 0x10); // track 2 control: audio
    CHECK_EQ(r.data[21], 0x10); // track 3 control: audio
    CHECK_EQ(r.data[29], 0x10); // leadout inherits audio control
    CHECK_EQ(r.data[30], 0xAA);
}
