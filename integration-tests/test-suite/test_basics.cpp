//
// test_basics.cpp
//
// INQUIRY, TEST UNIT READY / unit attention flow, REQUEST SENSE,
// READ CAPACITY, unknown opcodes, and BOT protocol basics (stall before
// CSW on CHECK CONDITION with a data phase, residue accounting).
//
#include "bench.h"
#include "framework.h"

// Struct layouts must match the device exactly for byte-level tests to
// mean anything.
static_assert(sizeof(TUSBCDCBW) == 31, "CBW must be 31 bytes");
static_assert(sizeof(TUSBCDCSW) == 13, "CSW must be 13 bytes");
static_assert(sizeof(TUSBCDInquiryReply) == 96, "INQUIRY reply must be 96 bytes");
static_assert(sizeof(ModePage0x2AData) == 68, "mode page 0x2A must be 68 bytes");
static_assert(sizeof(ModeSense10Header) == 8, "MODE SENSE(10) header must be 8 bytes");

TEST(inquiry_standard)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();

    // INQUIRY must work even while unit attention is pending.
    const u8 cdb[6] = {0x12, 0x00, 0x00, 0x00, 36, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 36);

    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r.data.size(), (size_t)36);
    CHECK_EQ(r.data[0], 0x05); // CD/DVD device
    CHECK_EQ(r.data[1], 0x80); // removable
    CHECK_BYTES(r.data.data() + 8, 8, "USBODE  ", 8);
    CHECK_BYTES(r.data.data() + 16, 16, "CDROM EMULATOR  ", 16);
}

TEST(unit_attention_flow)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();

    // 1. TEST UNIT READY under unit attention: CHECK CONDITION 06/28/00.
    const u8 tur[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(tur, sizeof(tur), 0);
    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK(!r.stalledIn); // no data phase expected -> no stall

    // 2. REQUEST SENSE reports 06/28/00 (medium changed) and clears it.
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.csw.bmCSWStatus, 0);
    CHECK_EQ(sense.data.size(), (size_t)18);
    CHECK_EQ(sense.data[0], 0x70); // current error, fixed format
    CHECK_EQ(sense.data[2], 0x06); // UNIT ATTENTION
    CHECK_EQ(sense.data[12], 0x28); // ASC: medium may have changed
    CHECK_EQ(sense.data[13], 0x00);

    // 3. TEST UNIT READY now succeeds.
    r = bench.SendCommand(tur, sizeof(tur), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
}

TEST(read_blocked_by_unit_attention)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();

    // READ(10) is on the blocked list while unit attention is pending;
    // the data phase must be stalled before the failing CSW (BOT 6.7.2).
    const u8 read10[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    auto r = bench.SendCommand(read10, sizeof(read10), 2048);
    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK(r.stalledIn);
    CHECK_EQ(r.csw.dCSWDataResidue, 2048u); // nothing was transferred
    CHECK_EQ(r.data.size(), (size_t)0);
}

TEST(read_capacity)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense(); // clear unit attention

    const u8 cdb[10] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 8);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    // Last LBA = leadout - 1 = 1199 = 0x04AF, block size 2048, big-endian.
    const u8 expected[8] = {0x00, 0x00, 0x04, 0xAF, 0x00, 0x00, 0x08, 0x00};
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(unknown_opcode)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[6] = {0xEE, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 1);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);  // ILLEGAL REQUEST
    CHECK_EQ(sense.data[12], 0x20); // INVALID COMMAND OPERATION CODE
}

TEST(get_configuration_cd_profile)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 256);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    // Full feature set for CD media (profile list + core + morphing +
    // removable medium + random readable + multi-read + CD read + power
    // management + analog audio play + real-time streaming) is 88 bytes;
    // the header reports 84 (total minus its own length field).
    CHECK_EQ(r.data.size(), (size_t)88);
    const u8 header[8] = {0x00, 0x00, 0x00, 84, 0x00, 0x00, 0x00, 0x08};
    CHECK_BYTES(r.data.data(), 8, header, 8);
}

TEST(toolbox_set_next_cd)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0xD8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(tbservice.setNextCDCalls, 1);
    CHECK_EQ(tbservice.lastSetNextCD, 3);
}
