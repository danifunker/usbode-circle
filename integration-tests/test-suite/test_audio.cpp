//
// test_audio.cpp
//
// The analog CD audio command set: PLAY AUDIO (MSF/10), READ SUB-CHANNEL
// position polling, PAUSE/RESUME, SEEK. This is the exact sequence retail
// Win98 SE's MCICDA driver issues (Trace Lab golden capture) and the
// sequence Win98 QuickInstall's replacement USB stack never sends
// (oerg866/win98-quickinstall#151).
//
#include "bench.h"
#include "framework.h"

TEST(play_audio_msf_reaches_player)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // Play track 2: LBA 3000..6000 -> MSF 00:42:00 .. 01:22:00.
    const u8 cdb[10] = {0x47, 0x00, 0x00, 0x00, 42, 0x00, 0x01, 22, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.playCalls, 1);
    CHECK_EQ(player.lastPlayLBA, 3000u);
    CHECK_EQ(player.lastPlayBlocks, 3000u);
}

TEST(play_audio_10_reaches_player)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // PLAY AUDIO(10): LBA 3000, 500 blocks.
    const u8 cdb[10] = {0x45, 0x00, 0x00, 0x00, 0x0B, 0xB8, 0x00, 0x01, 0xF4, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.playCalls, 1);
    CHECK_EQ(player.lastPlayLBA, 3000u);
    CHECK_EQ(player.lastPlayBlocks, 500u);
}

// PLAY AUDIO parked the track length in the counter onXferCmplt uses to decide
// "more data owed" vs "send the CSW", so the next command streamed raw sectors.
TEST(play_audio_does_not_leave_a_read_pending)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb10[10] = {0x45, 0x00, 0x00, 0x00, 0x0B, 0xB8, 0x00, 0x01, 0xF4, 0x00};
    auto play = bench.SendCommand(cdb10, sizeof(cdb10), 0);
    CHECK_EQ(play.csw.bmCSWStatus, 0);
    CHECK_EQ(player.playCalls, 1);

    // MECHANISM STATUS pays for it: it has a data phase and, unlike INQUIRY or
    // READ TOC, does not zero the counter itself.
    const u8 mech[12] = {0xBD, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0};
    auto ms = bench.SendCommand(mech, sizeof(mech), 8);
    CHECK(ms.gotCSW);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data.size(), (size_t)8);
}

// PLAY AUDIO(12) carries the count in a 4-byte field and had the same problem.
TEST(play_audio_12_does_not_leave_a_read_pending)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb12[12] = {0xA5, 0x00, 0x00, 0x00, 0x0B, 0xB8,
                          0x00, 0x00, 0x01, 0xF4, 0x00, 0x00};
    auto play = bench.SendCommand(cdb12, sizeof(cdb12), 0);
    CHECK_EQ(play.csw.bmCSWStatus, 0);

    const u8 mech[12] = {0xBD, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0};
    auto ms = bench.SendCommand(mech, sizeof(mech), 8);
    CHECK(ms.gotCSW);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data.size(), (size_t)8);
}

// Skipping tracks drives the whole audio-control family; none has a data phase,
// so none may leave a count standing for the next command to act on.
TEST(audio_control_commands_leave_no_read_pending)
{
    struct Case { const char *name; u8 cdb[12]; size_t len; };
    const Case cases[] = {
        {"PLAY AUDIO(10)",  {0x45, 0, 0, 0, 0x0B, 0xB8, 0, 0x01, 0xF4, 0, 0, 0}, 10},
        {"PLAY AUDIO(12)",  {0xA5, 0, 0, 0, 0x0B, 0xB8, 0, 0, 0x01, 0xF4, 0, 0}, 12},
        {"PLAY AUDIO MSF",  {0x47, 0, 0, 0, 2, 0, 0, 4, 0, 0, 0, 0}, 10},
        {"SEEK(10)",        {0x2B, 0, 0, 0, 0x0B, 0xB8, 0, 0, 0, 0, 0, 0}, 10},
        {"PAUSE/RESUME",    {0x4B, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0}, 10},
        {"STOP PLAY/SCAN",  {0x4E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 10},
    };

    for (const Case &c : cases) {
        CFakeImageDevice *disc = MakeAudioCD(3, 3000);
        CCDPlayer player;
        CGadgetTestBench bench(disc, false, &player);
        bench.Activate();
        bench.RequestSense();

        // A multi-batch read still owing blocks, which is the state a host that
        // is reading the disc while playing leaves behind.
        bench.SetPendingBlocks(500);
        auto ctrl = bench.SendCommand(c.cdb, c.len, 0);
        CHECK(ctrl.gotCSW);

        const u8 mech[12] = {0xBD, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0};
        auto ms = bench.SendCommand(mech, sizeof(mech), 8);
        if (!ms.gotCSW || ms.data.size() != 8) {
            ReportFailure(__FILE__, __LINE__,
                          std::string(c.name) + " left a read pending: got " +
                              std::to_string(ms.data.size()) + " bytes, expected 8");
        }
    }
}

TEST(play_audio_on_data_track_fails)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x45, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x40, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);

    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK_EQ(player.playCalls, 0);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);
    CHECK_EQ(sense.data[12], 0x64); // ILLEGAL MODE FOR THIS TRACK
}

TEST(read_subchannel_position_playing)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // Pretend the player is 1500 sectors into track 2.
    player.state = CCDPlayer::PLAYING;
    player.currentAddress = 4500;

    // READ SUB-CHANNEL, MSF, current position, alloc 16 — Win98 polls
    // this every ~200 ms while the CD Player window is open.
    const u8 cdb[10] = {0x42, 0x02, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 16, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 16);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[16] = {
        0x00, 0x11, // audio status: playing
        0x00, 0x0C, // 12 bytes of position data follow
        0x01,       // format: current position
        0x10,       // ADR 1, control: audio
        0x02,       // track 2
        0x01,       // index 1
        0x00, 0x01, 0x02, 0x00, // absolute: MSF 01:02:00 (LBA 4500 + pregap)
        0x00, 0x00, 0x14, 0x00, // relative: MSF 00:20:00 (1500 into track)
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(read_subchannel_status_paused)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    player.state = CCDPlayer::PAUSED;
    player.currentAddress = 0;

    const u8 cdb[10] = {0x42, 0x02, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 16, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 16);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data[1], 0x12); // paused
}

TEST(pause_resume_and_seek)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // PAUSE (resume bit clear)
    const u8 pause[10] = {0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(pause, sizeof(pause), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.pauseCalls, 1);

    // RESUME
    const u8 resume[10] = {0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    r = bench.SendCommand(resume, sizeof(resume), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.resumeCalls, 1);

    // SEEK to LBA 6000 (track 3)
    const u8 seek[10] = {0x2B, 0x00, 0x00, 0x00, 0x17, 0x70, 0x00, 0x00, 0x00, 0x00};
    r = bench.SendCommand(seek, sizeof(seek), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.seekCalls, 1);
    CHECK_EQ(player.lastSeekLBA, 6000u);
}

TEST(read_disc_information_audio)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 34);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)34);
    CHECK_EQ(r.data[2], 0x0E); // complete, finalized
    CHECK_EQ(r.data[3], 0x01); // first track
    CHECK_EQ(r.data[4], 0x01); // one session
    CHECK_EQ(r.data[6], 0x03); // last track in last session
    CHECK_EQ(r.data[8], 0x00); // disc type: CD-DA
}
