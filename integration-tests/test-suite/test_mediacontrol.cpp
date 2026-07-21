//
// test_mediacontrol.cpp
//
// The commands a host sends *around* reading: START STOP UNIT (0x1B),
// PREVENT ALLOW MEDIUM REMOVAL (0x1E), and MODE SELECT (0x55).
//
// These are the handshake commands, not the data path, and they have a
// particular failure mode: a host issues them during mount, on eject, and
// before playing audio, and reacts badly when they are answered wrongly even
// though nothing about the disc is broken. Windows sends START STOP UNIT
// with START=1 as part of bringing the drive online, and PREVENT ALLOW
// MEDIUM REMOVAL whenever a volume is opened.
//
// USBODE has no tray and no motor, so all three are largely no-ops. The point
// of pinning them is the opposite of the usual one: it is the *no-op* that is
// load-bearing. An eject that really ejected, or a lock that really locked,
// would break the web UI's disc swap and leave Windows holding a drive it
// cannot use.
//
// NOTE: two MODE SELECT tests are held out here, for the same reason as in
// test_toolbox.cpp -- they fail today. ProcessOut() selects the mode page to
// apply by reading m_OutBuffer[9], which is the page's *length* byte; the
// page *code* is at m_OutBuffer[8] (and the struct it casts, ModePage0x0EData,
// starts there). This works only because the CD Audio Control page's standard
// length, 0x0E, happens to equal its page code. The two held-out tests assert
// the mirror-image cases, both verified to fail today: an audio page sent
// with any other declared length must still apply, and an unrelated page that
// declares length 0x0E must not move the volume. They belong with the
// one-line fix -- `m_OutBuffer[8] & 0x3F` -- on the firmware branch.
//
#include "bench.h"
#include "framework.h"

#include <circle/timer.h>

#include <cstring>
#include <vector>

// The gadget latches the swap 100 us after SetDevice(); Update() is the
// task-level poll that notices. Same helper as test_mediachange.cpp.
static void SettleDiscSwap(CGadgetTestBench &bench)
{
    CTimer::Get()->TestAdvanceTicks(CLOCKHZ / 10000 + 1);
    bench.gadget->Update();
}

static CGadgetTestBench::Result StartStopUnit(CGadgetTestBench &bench, u8 flags,
                                              u8 immed = 0)
{
    const u8 cdb[6] = {0x1B, immed, 0x00, 0x00, flags, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 0);
}

static CGadgetTestBench::Result PreventAllow(CGadgetTestBench &bench, u8 prevent)
{
    const u8 cdb[6] = {0x1E, 0x00, 0x00, 0x00, prevent, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 0);
}

static CGadgetTestBench::Result TestUnitReady(CGadgetTestBench &bench)
{
    const u8 cdb[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 0);
}

// Build a MODE SELECT(10) parameter list: 8-byte header, then one mode page.
static std::vector<u8> ModeSelectPayload(u8 pageCode, u8 pageLength,
                                         u8 vol0 = 0, u8 vol1 = 0)
{
    std::vector<u8> payload(24, 0);
    payload[8] = pageCode;
    payload[9] = pageLength;
    payload[16] = 0x01; // CDDA output 0 channel select
    payload[17] = vol0;
    payload[18] = 0x02; // CDDA output 1 channel select
    payload[19] = vol1;
    return payload;
}

static CGadgetTestBench::Result ModeSelect10(CGadgetTestBench &bench,
                                             const std::vector<u8> &payload)
{
    const u8 cdb[10] = {0x55, 0x10, 0x00, 0x00, 0x00, 0x00,
                        0x00, (u8)(payload.size() >> 8), (u8)payload.size(), 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), (u32)payload.size(), false,
                             payload.data(), payload.size());
}

// ---------------------------------------------------------------------------
// START STOP UNIT
// ---------------------------------------------------------------------------

// Windows issues START STOP UNIT with START=1 while bringing the drive
// online, and DOS drivers send it before their first read. All four
// LOEJ/START combinations are legal and none of them is an error for a device
// with no tray; answering CHECK CONDITION to any of them can abort the mount.
TEST(start_stop_unit_all_combinations_accepted)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(StartStopUnit(bench, 0x00).csw.bmCSWStatus, 0); // stop
    CHECK_EQ(StartStopUnit(bench, 0x01).csw.bmCSWStatus, 0); // start
    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0); // eject (LOEJ=1)
    CHECK_EQ(StartStopUnit(bench, 0x03).csw.bmCSWStatus, 0); // load
}

// The IMMED bit asks the drive to return status before the mechanism has
// finished moving. There is no mechanism here, so it changes nothing, but a
// host that sets it still expects a normal CSW.
TEST(start_stop_unit_immed_accepted)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    auto r = StartStopUnit(bench, 0x01, 0x01);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
}

// The load-bearing no-op. USBODE's "disc" is a file chosen in the web UI, and
// there is nothing to eject: after a host asks for an eject the drive must
// still be ready and still serving the same image. Implementing eject
// literally -- dropping to NO_MEDIUM on LOEJ=1 -- would mean a user pressing
// Eject in Explorer, or any host that ejects during shutdown, leaves the
// drive dead until it is unplugged.
TEST(start_stop_unit_eject_leaves_medium_readable)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 capCDB[10] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto before = bench.SendCommand(capCDB, sizeof(capCDB), 8);
    CHECK_EQ(before.csw.bmCSWStatus, 0);

    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0); // LOEJ=1, START=0

    // Still ready, same capacity, and the data still reads correctly.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);

    auto after = bench.SendCommand(capCDB, sizeof(capCDB), 8);
    CHECK_EQ(after.csw.bmCSWStatus, 0);
    CHECK_BYTES(after.data.data(), after.data.size(),
                before.data.data(), before.data.size());

    const u8 readCDB[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x05,
                            0x00, 0x00, 0x01, 0x00};
    auto read = bench.SendCommand(readCDB, sizeof(readCDB), 2048);
    CHECK_EQ(read.csw.bmCSWStatus, 0);
    CHECK_EQ(read.data.size(), (size_t)2048);
    u8 expected[2048];
    FillPatternSector(expected, 5, 2048);
    CHECK_BYTES(read.data.data(), read.data.size(), expected, sizeof(expected));
}

// ---------------------------------------------------------------------------
// PREVENT ALLOW MEDIUM REMOVAL
// ---------------------------------------------------------------------------

// Windows locks the door whenever a volume is open and unlocks it on close.
// Both directions must be accepted; a CHECK CONDITION here shows up as a
// failure to open the drive in Explorer.
TEST(prevent_allow_medium_removal_both_states_accepted)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(PreventAllow(bench, 0x01).csw.bmCSWStatus, 0); // lock
    CHECK_EQ(PreventAllow(bench, 0x00).csw.bmCSWStatus, 0); // unlock
}

// The other load-bearing no-op. On USBODE the disc is swapped from the web UI
// (or the toolbox), out of band from the USB host -- and the host usually has
// the door "locked" at that moment, because it has the volume mounted. A lock
// that really locked would make the web UI's swap silently fail exactly when
// users reach for it. This drives the full swap with the lock held and
// asserts the new disc is what the host reads afterwards.
TEST(medium_removal_lock_does_not_block_disc_swap)
{
    CFakeImageDevice *discA = MakeDataISO(500);
    CFakeImageDevice *discB = MakeDataISO(1500);
    CGadgetTestBench bench(discA);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(PreventAllow(bench, 0x01).csw.bmCSWStatus, 0); // host locks the door

    bench.gadget->SetDevice(discB); // web-UI mount; deletes disc A
    SettleDiscSwap(bench);

    // Media change is reported, then cleared the way a host would.
    auto blocked = TestUnitReady(bench);
    CHECK_EQ(blocked.csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x06);  // UNIT ATTENTION
    CHECK_EQ(sense.data[12], 0x28); // NOT READY TO READY CHANGE
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);

    // The capacity the host now sees is disc B's, not disc A's.
    const u8 capCDB[10] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto cap = bench.SendCommand(capCDB, sizeof(capCDB), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) |
                  (cap.data[2] << 8) | cap.data[3];
    CHECK_EQ(lastLBA, 1499u);
}

// ---------------------------------------------------------------------------
// MODE SELECT
// ---------------------------------------------------------------------------

// Descent 2 changes the volume by sending four MODE SELECTs back to back with
// the two channels swapped, so neither one on its own carries the intended
// level; the gadget takes the lower of the pair each time. This replays the
// exact sequence from the comment in ProcessOut() and checks where it lands.
TEST(mode_select10_descent2_volume_sequence)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0E, 0, 255)).csw.bmCSWStatus, 0);
    CHECK_EQ(player.volume, 0);
    CHECK_EQ(ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0E, 255, 0)).csw.bmCSWStatus, 0);
    CHECK_EQ(player.volume, 0);
    CHECK_EQ(ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0E, 74, 255)).csw.bmCSWStatus, 0);
    CHECK_EQ(player.volume, 74);
    CHECK_EQ(ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0E, 255, 74)).csw.bmCSWStatus, 0);
    CHECK_EQ(player.volume, 74);

    CHECK_EQ(player.setVolumeCalls, 4);
}

// A MODE SELECT carrying some other page is not an error -- hosts set caching
// and error-recovery parameters routinely -- but it must not be mistaken for
// the audio page and move the volume.
//
// This case passes both before and after the page-code fix described at the
// top of the file, since page 0x01 differs from the audio page in code *and*
// in declared length. The sharper version, an unrelated page that declares
// length 0x0E, is one of the held-out tests.
TEST(mode_select10_unrelated_page_does_not_touch_volume)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // Page 0x01 (read error recovery), page length 0x0A.
    auto r = ModeSelect10(bench, ModeSelectPayload(0x01, 0x0A, 5, 5));
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 0);
}

// A parameter list length of zero is legal: the host is setting nothing. It
// must still complete with GOOD status and leave the drive usable, rather
// than opening a data-out phase that never gets filled and stalling the
// endpoint mid-command.
TEST(mode_select10_zero_length_parameter_list)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x55, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0, false, nullptr, 0);
    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);

    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);
}

// The data-out phase has to be consumed and accounted for, or the host is
// left waiting: the CSW must arrive and the residue must show the whole
// parameter list was taken.
TEST(mode_select10_consumes_data_phase)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    auto r = ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0E, 42, 42));
    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    CHECK(!r.stalledIn);
    CHECK(!r.stalledOut);

    // The drive keeps working afterwards -- the OUT endpoint is re-armed for
    // the next CBW rather than left in the data-out state.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);
}

// MODE SELECT(6) (0x15) is not implemented. That is defensible -- plenty of
// real MMC drives are 10-byte only, and hosts fall back -- but it has to be
// refused cleanly with INVALID COMMAND OPERATION CODE so the host knows to
// fall back, rather than being silently swallowed.
TEST(mode_select6_rejected_as_invalid_opcode)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[6] = {0x15, 0x10, 0x00, 0x00, 24, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 0);
    CHECK_EQ(r.csw.bmCSWStatus, 1);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);  // ILLEGAL REQUEST
    CHECK_EQ(sense.data[12], 0x20); // INVALID COMMAND OPERATION CODE
}
