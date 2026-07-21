//
// test_modesense.cpp
//
// MODE SENSE(6)/(10) and MODE SELECT(10). Locks in the two Win9x CD-audio
// regressions fixed after 3.2.0:
//   - medium type byte reflects the actual disc (issue #164: hardcoded
//     0x13 made Win98 MCICDA treat every disc as data-only)
//   - MODE SENSE(10) responses are padded to the allocation length
//     (Win9x usbstor.sys rejects short data phases and retries forever)
//
#include "bench.h"
#include "framework.h"

TEST(mode_sense10_medium_type_data_cd)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x5A, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data[2], 0x01); // medium type: data CD

    // Header + page 0x2A = 8 + 68 = 76 real bytes; mode data length
    // reports the true length...
    CHECK_EQ(r.data[0], 0x00);
    CHECK_EQ(r.data[1], 74); // 76 - 2
    // ...but the data phase is padded to the allocation length.
    CHECK_EQ(r.data.size(), (size_t)128);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    for (size_t i = 76; i < 128; i++)
    {
        CHECK_EQ(r.data[i], 0x00);
    }

    // Page 0x2A starts after the 8-byte header.
    CHECK_EQ(r.data[8], 0x2A);
    CHECK_EQ(r.data[9], 0x42);
    CHECK_EQ(r.data[10], 0x07); // CD media capability byte
}

TEST(mode_sense10_medium_type_audio_cd)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x5A, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data[2], 0x02); // medium type: audio CD
}

TEST(mode_sense10_medium_type_mixed_cd)
{
    CFakeImageDevice *disc = MakeMixedModeCD(1000, 2, 2000);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x5A, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data[2], 0x03); // medium type: mixed data+audio
}

TEST(mode_sense10_page0e_win98_golden)
{
    // Exact request retail Win98 SE sends before playing audio (from the
    // Trace Lab golden capture): MODE SENSE(10) page 0x0E, alloc 0x18.
    // The full 24-byte response below is what a working Win98 SE host
    // accepted right before issuing PLAY AUDIO MSF.
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x5A, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0x18);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[24] = {
        0x00, 0x16, // mode data length 22
        0x02,       // medium type: audio CD
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x0E, 0x0E, // page 0x0E, length 14
        0x04,       // IMMED
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0xFF, // output 0 -> channel 0, max volume
        0x02, 0xFF, // output 1 -> channel 1, max volume
        0x00, 0x00, 0x00, 0x00,
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
}

TEST(mode_sense10_unsupported_page)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x5A, 0x00, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);

    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK(r.stalledIn);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);
    CHECK_EQ(sense.data[12], 0x24); // INVALID FIELD IN CDB
}

TEST(mode_sense10_saved_values_unsupported)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Page control 0b11 = saved values.
    const u8 cdb[10] = {0x5A, 0x00, (u8)(0xC0 | 0x2A), 0x00, 0x00, 0x00, 0x00, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);

    CHECK_EQ(r.csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);
    CHECK_EQ(sense.data[12], 0x39); // SAVING PARAMETERS NOT SUPPORTED
}

TEST(mode_sense6_no_padding)
{
    // The padding fix applies to MODE SENSE(10) only; MODE SENSE(6)
    // keeps the classic short response with a nonzero residue.
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[6] = {0x1A, 0x00, 0x2A, 0x00, 128, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 128);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)72); // 4-byte header + 68-byte page
    CHECK_EQ(r.data[0], 71);             // mode data length
    CHECK_EQ(r.data[1], 0x01);           // medium type
    CHECK_EQ(r.csw.dCSWDataResidue, 56u);
}

TEST(mode_select10_sets_player_volume)
{
    // Retail Win98 writes the CD volume via MODE SELECT(10) page 0x0E
    // before playing. The gadget picks the lower of the two channel
    // volumes (Descent 2 quirk).
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    u8 payload[24];
    memset(payload, 0, sizeof(payload));
    payload[8] = 0x0E;  // page code
    payload[9] = 0x0E;  // page length
    payload[16] = 0x01; // output 0 channel
    payload[17] = 100;  // output 0 volume
    payload[18] = 0x02; // output 1 channel
    payload[19] = 255;  // output 1 volume

    const u8 cdb[10] = {0x55, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 24, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 24, false, payload, sizeof(payload));

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 1);
    CHECK_EQ(player.volume, 100);
}
