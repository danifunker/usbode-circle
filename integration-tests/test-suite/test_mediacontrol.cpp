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
// USBODE has no tray and no motor. START STOP UNIT with LOEJ=1/START=0
// performs a real eject: the drive drops to NO_MEDIUM (MEDIUM NOT PRESENT)
// while keeping the image file internally. This lets a host, a game, or the
// device's own button present an empty drive to fix hosts that cache a stale
// disc across a swap.
//
// LOEJ=1/START=1 (load) is accepted but deliberately does NOT bring the medium
// back: an eject models the user taking the disc out, so the tray is empty.
// Re-inserting is a physical act -- the button, the web UI, or mounting an
// image -- which all go through Insert() and the normal media-change
// (UNIT ATTENTION -> READY) sequence.
//
// The door lock still matters, though: like a real drive, a host eject is
// refused (05/53/02, MEDIUM REMOVAL PREVENTED) while the host holds a PREVENT
// ALLOW MEDIUM REMOVAL lock. The web UI / button eject bypass the lock on
// purpose (there is no physical pinhole), and a lock must never block the web
// UI's out-of-band disc swap.
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

// A host eject (LOEJ=1, START=0) really ejects: the command itself succeeds,
// but the drive then reports NO_MEDIUM (02/3a/00, MEDIUM NOT PRESENT) and
// refuses reads, exactly as an empty drive would. This is what lets a host or
// a game clear a stale cached disc.
TEST(start_stop_unit_eject_reports_no_medium)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // The eject command completes with GOOD status.
    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0); // LOEJ=1, START=0

    // The drive now reads as empty.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x02);  // NOT READY
    CHECK_EQ(sense.data[12], 0x3a); // MEDIUM NOT PRESENT

    // Reads are refused while ejected.
    const u8 readCDB[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x05,
                            0x00, 0x00, 0x01, 0x00};
    auto read = bench.SendCommand(readCDB, sizeof(readCDB), 2048);
    CHECK_EQ(read.csw.bmCSWStatus, 1);
}

// LOEJ=1, START=1 (load) closes an empty tray: it succeeds, but it does NOT
// bring the medium back. Ejecting on USBODE models the user taking the disc
// out, so there is nothing left for the host to load. This matters because
// hosts poll with LOAD while hunting for new media -- macOS does it after its
// own eject -- and honoring it made the drive silently re-mount the image the
// user had just ejected, then persist "inserted" over the saved ejected state.
TEST(start_stop_unit_load_after_eject_leaves_drive_empty)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0); // eject
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);       // empty
    bench.RequestSense();

    CHECK_EQ(StartStopUnit(bench, 0x03).csw.bmCSWStatus, 0); // load
    SettleDiscSwap(bench);

    // Still empty, and still reporting MEDIUM NOT PRESENT.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x02);  // NOT READY
    CHECK_EQ(sense.data[12], 0x3a); // MEDIUM NOT PRESENT
    CHECK_EQ(bench.gadget->IsEjected(), true);

    // Repeated LOAD polling cannot wear the eject down either.
    for (int i = 0; i < 5; ++i)
    {
        CHECK_EQ(StartStopUnit(bench, 0x03).csw.bmCSWStatus, 0);
        SettleDiscSwap(bench);
    }
    CHECK_EQ(bench.gadget->IsEjected(), true);
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
}

// Insert() -- the button, the web UI, mounting an image -- is the only way the
// medium comes back. It comes back through the same media-change path a disc
// swap uses: UNIT ATTENTION reported once, cleared by REQUEST SENSE, then READY
// and readable again, serving the same image that was ejected.
TEST(device_insert_after_eject_restores_medium)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0); // eject
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);       // empty
    bench.RequestSense();

    bench.gadget->Insert();
    SettleDiscSwap(bench);

    // Media change reported, then cleared the way a host would.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x06);  // UNIT ATTENTION
    CHECK_EQ(sense.data[12], 0x28); // NOT READY TO READY CHANGE
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);

    // The same image reads correctly again.
    const u8 readCDB[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x05,
                            0x00, 0x00, 0x01, 0x00};
    auto read = bench.SendCommand(readCDB, sizeof(readCDB), 2048);
    CHECK_EQ(read.csw.bmCSWStatus, 0);
    CHECK_EQ(read.data.size(), (size_t)2048);
    u8 expected[2048];
    FillPatternSector(expected, 5, 2048);
    CHECK_BYTES(read.data.data(), read.data.size(), expected, sizeof(expected));
}

// An ejected drive must not answer any of the "is there a disc?" probes as if
// one were loaded. GET CONFIGURATION's Current Profile is the decisive one on
// macOS: it reported CD-ROM (0x0008) unconditionally, which reads as "medium
// present" no matter what TEST UNIT READY says, and macOS re-mounted the image
// on every poll. MMC requires 0000h with no medium, and no profile descriptor
// marked current.
TEST(ejected_drive_reports_no_current_profile)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 getConfig[10] = {0x46, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x20, 0x00};

    // With a disc loaded the CD-ROM profile is current.
    auto loaded = bench.SendCommand(getConfig, sizeof(getConfig), 0x20);
    CHECK_EQ(loaded.csw.bmCSWStatus, 0);
    CHECK_EQ((loaded.data[6] << 8) | loaded.data[7], 0x0008); // PROFILE_CDROM

    bench.gadget->Eject();

    auto empty = bench.SendCommand(getConfig, sizeof(getConfig), 0x20);
    CHECK_EQ(empty.csw.bmCSWStatus, 0);
    CHECK_EQ((empty.data[6] << 8) | empty.data[7], 0x0000); // no current profile

    // The profile descriptor inside the Profile List feature must agree: byte
    // 2 of each descriptor carries the Current bit in bit 0. Header is 8 bytes,
    // then the feature header is 4, then the first profile descriptor.
    CHECK_EQ(empty.data[8 + 4 + 2] & 0x01, 0);
}

// The other media-presence probes an ejected drive must refuse. READ DISC
// INFORMATION described a finalized single-session disc while the drive was
// empty; macOS calls it while working out the media type.
TEST(ejected_drive_refuses_disc_probes)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    bench.gadget->Eject();

    const u8 discInfo[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x20, 0x00};
    CHECK_EQ(bench.SendCommand(discInfo, sizeof(discInfo), 0x20).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x02);  // NOT READY
    CHECK_EQ(sense.data[12], 0x3a); // MEDIUM NOT PRESENT

    const u8 subChannel[10] = {0x42, 0x02, 0x40, 0x01, 0x00, 0x00,
                               0x00, 0x00, 0x10, 0x00};
    CHECK_EQ(bench.SendCommand(subChannel, sizeof(subChannel), 0x10).csw.bmCSWStatus, 1);
    bench.RequestSense();

    // MODE SENSE still answers (it describes the drive, not the disc), but the
    // medium type byte must not claim a data or audio disc is loaded.
    const u8 modeSense[6] = {0x1A, 0x00, 0x2A, 0x00, 0x20, 0x00};
    auto ms = bench.SendCommand(modeSense, sizeof(modeSense), 0x20);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[1], 0x00); // medium type: none
}

// Boot restore: the drive was ejected at power-off, so it must come up empty
// even though the remembered image is loaded behind the scenes. The arm is set
// before SetDevice() precisely so there is no window -- not even across
// enumeration -- in which the host can see a disc and mount it.
TEST(boot_eject_arm_keeps_drive_empty_through_enumeration)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, nullptr, false,
                           /* bBootEjected */ true);
    bench.Activate(); // enumeration: OnActivate() must not make the disc ready

    CHECK_EQ(bench.gadget->IsEjected(), true);
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x02);  // NOT READY
    CHECK_EQ(sense.data[12], 0x3a); // MEDIUM NOT PRESENT

    // A host LOAD still cannot conjure the disc back at boot.
    CHECK_EQ(StartStopUnit(bench, 0x03).csw.bmCSWStatus, 0);
    SettleDiscSwap(bench);
    CHECK_EQ(bench.gadget->IsEjected(), true);

    // The user inserting restores the remembered image immediately - it was
    // loaded all along, which is why Insert needs no disc reload.
    bench.gadget->Insert();
    SettleDiscSwap(bench);
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    bench.RequestSense();
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);
    delete disc;
}

// The arm is one-shot. A later deliberate mount (user picks an image) must
// insert normally rather than inheriting the boot-time eject.
TEST(boot_eject_arm_is_consumed_by_first_set_device)
{
    CFakeImageDevice *first = MakeDataISO(1200);
    CFakeImageDevice *second = MakeDataISO(1200);
    CGadgetTestBench bench(first, false, nullptr, nullptr, nullptr, false,
                           /* bBootEjected */ true);
    bench.Activate();
    CHECK_EQ(bench.gadget->IsEjected(), true);

    // User mounts a different image: that is a disc insertion.
    bench.gadget->SetDevice(second);
    SettleDiscSwap(bench);
    CHECK_EQ(bench.gadget->IsEjected(), false);
    delete second;
}

// Like a real drive, a host-issued eject is refused while the host holds the
// PREVENT ALLOW MEDIUM REMOVAL lock: CHECK CONDITION 05/53/02, and the medium
// stays readable. Once the host unlocks, the eject goes through.
TEST(start_stop_unit_eject_refused_while_locked)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(PreventAllow(bench, 0x01).csw.bmCSWStatus, 0); // lock

    auto refused = StartStopUnit(bench, 0x02); // eject while locked
    CHECK_EQ(refused.csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x05);  // ILLEGAL REQUEST
    CHECK_EQ(sense.data[12], 0x53); // MEDIA LOAD OR EJECT FAILED
    CHECK_EQ(sense.data[13], 0x02); // MEDIUM REMOVAL PREVENTED

    // Still readable - the eject did not happen.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);

    // After unlocking, the eject succeeds.
    CHECK_EQ(PreventAllow(bench, 0x00).csw.bmCSWStatus, 0); // unlock
    CHECK_EQ(StartStopUnit(bench, 0x02).csw.bmCSWStatus, 0);
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1); // now empty
}

// The device's own eject (button / web UI) is the emergency override: it must
// work even while the host holds the removal lock, since there is no physical
// pinhole. Exercised directly through the gadget's Eject(), which is what the
// button and web paths call.
TEST(device_eject_overrides_medium_removal_lock)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(PreventAllow(bench, 0x01).csw.bmCSWStatus, 0); // host locks

    bench.gadget->Eject(); // device-initiated eject bypasses the lock

    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data[2], 0x02);  // NOT READY
    CHECK_EQ(sense.data[12], 0x3a); // MEDIUM NOT PRESENT
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
// Page 0x01 differs from the audio page in code *and* in declared length, so
// this passes either way; mode_select10_page_length_0e_is_not_the_audio_page
// below is the sharper version.
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

// The page code identifies the page. The declared length is the initiator's
// business and may legitimately be shorter than the full page, so the audio
// page must still be applied when a host declares some other length. The
// firmware used to dispatch on the length byte, which silently dropped the
// host's volume change in exactly this case.
TEST(mode_select10_audio_page_with_nonstandard_length_still_applies)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    auto r = ModeSelect10(bench, ModeSelectPayload(0x0E, 0x0A, 55, 55));
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 1);
    CHECK_EQ(player.volume, 55);
}

// The same bug in the other direction: page 0x0D (CD-ROM parameters)
// declaring a 14-byte page is not the audio page, and must not move the
// volume just because its length matches the audio page's code.
TEST(mode_select10_page_length_0e_is_not_the_audio_page)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    auto r = ModeSelect10(bench, ModeSelectPayload(0x0D, 0x0E, 7, 7));
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 0);
}

// When the host includes block descriptors, the mode page starts after them,
// not at a fixed offset 8. Reading the page code at 8 regardless would find a
// block descriptor byte instead and either miss the page or act on the wrong
// one.
TEST(mode_select10_audio_page_after_block_descriptor)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // 8-byte header declaring one 8-byte block descriptor, then the audio page.
    std::vector<u8> payload(8 + 8 + 16, 0);
    payload[7] = 8; // block descriptor length
    payload[8 + 0] = 0x00; // density code, then a 2048-byte block length
    payload[8 + 6] = 0x08;
    payload[16 + 0] = 0x0E; // page code
    payload[16 + 1] = 0x0E; // page length
    payload[16 + 8] = 0x01; // CDDA output 0 channel select
    payload[16 + 9] = 33;   // volume 0
    payload[16 + 10] = 0x02;
    payload[16 + 11] = 33;  // volume 1

    auto r = ModeSelect10(bench, payload);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 1);
    CHECK_EQ(player.volume, 33);
}

// A truncated audio page must be ignored rather than read past what arrived.
TEST(mode_select10_truncated_audio_page_ignored)
{
    CFakeImageDevice *disc = MakeAudioCD(3, 3000);
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // Header plus only the page code and length: the volume fields never
    // arrived, so there is nothing to apply.
    std::vector<u8> payload(10, 0);
    payload[8] = 0x0E;
    payload[9] = 0x0E;

    auto r = ModeSelect10(bench, payload);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(player.setVolumeCalls, 0);
}
