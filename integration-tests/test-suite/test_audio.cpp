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
