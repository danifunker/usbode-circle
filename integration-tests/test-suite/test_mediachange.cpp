//
// test_mediachange.cpp
//
// Disc-swap (media change) behaviour and GET EVENT STATUS NOTIFICATION.
//
// USBODE's whole reason to exist is letting the host swap the mounted image
// at runtime. When SetDevice() installs a different disc, the gadget must
// walk the host through the MMC media-change handshake:
//
//   1. eject     -> NO_MEDIUM, sense 02/3a/00 (MEDIUM NOT PRESENT)
//   2. settle    -> after a 100 ms window the new medium appears with a
//                   UNIT ATTENTION (sense 06/28/00) that BLOCKS reads
//   3. consume   -> REQUEST SENSE reports 06/28/00 and clears to READY,
//                   after which TOC / READ CAPACITY / READ(10) reflect the
//                   NEW disc, not the old one.
//
// GET EVENT STATUS NOTIFICATION (0x4A) is how Windows and macOS poll for
// that change: NewMedia while a swap is pending, Media Removal while ejected,
// No Change once steady. If any of this regresses, swapping a disc in the
// web UI silently keeps serving the previous disc's contents and TOC -- a
// miserable, invisible field bug that no other test in the suite would catch.
//
// SetDevice() here is the exact entry point the mount path uses on hardware,
// and CTimer is virtual so the settle window is exercised deterministically.
//
#include "bench.h"
#include "framework.h"

#include <circle/timer.h>

// The disc-swap settle window is 100 ms (CLOCKHZ/10000 ticks). Advancing the
// virtual clock past it and pumping Update() (the task-level poll that runs
// the swap state machine) drives NO_MEDIUM -> UNIT_ATTENTION.
static void SettleDiscSwap(CGadgetTestBench &bench)
{
    CTimer::Get()->TestAdvanceTicks(CLOCKHZ / 10000 + 1);
    bench.gadget->Update();
}

// Poll GESN for the media class (CDB[4] bit 4), polled bit set, allocation 8.
static CGadgetTestBench::Result PollMediaEvent(CGadgetTestBench &bench)
{
    const u8 cdb[10] = {0x4A, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 8);
}

static CGadgetTestBench::Result TestUnitReady(CGadgetTestBench &bench)
{
    const u8 cdb[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 0);
}

static CGadgetTestBench::Result Read10LBA0(CGadgetTestBench &bench)
{
    const u8 cdb[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), 2048);
}

// The full runtime disc-swap handshake, asserted state by state.
TEST(disc_swap_media_change_full_sequence)
{
    // Disc A: a small data CD. Mount, enumerate, clear the power-on UA.
    CFakeImageDevice *discA = MakeDataISO(500);
    CGadgetTestBench bench(discA);
    bench.Activate();
    bench.RequestSense(); // clear the initial UNIT ATTENTION -> READY

    // Enumeration armed a NewMedia event; drain it so the latch is in a known
    // state. This also asserts the initial-media announcement the host sees.
    {
        auto r = PollMediaEvent(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        const u8 newmedia[8] = {0x00, 0x06, 0x04, 0xDE, 0x02, 0x02, 0x00, 0x00};
        CHECK_BYTES(r.data.data(), r.data.size(), newmedia, sizeof(newmedia));
    }
    // Steady state, disc A ready: GESN reports No Change.
    {
        auto r = PollMediaEvent(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        const u8 nochange[8] = {0x00, 0x06, 0x04, 0xDE, 0x00, 0x02, 0x00, 0x00};
        CHECK_BYTES(r.data.data(), r.data.size(), nochange, sizeof(nochange));
    }
    // Sanity: with disc A ready, READ(10) of LBA 0 succeeds (2048 bytes).
    {
        auto r = Read10LBA0(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK_EQ(r.data.size(), (size_t)2048);
    }

    // --- Swap in disc B (a larger data CD). SetDevice() deletes disc A; do
    //     not touch discA again after this point. ---
    CFakeImageDevice *discB = MakeDataISO(1500);
    bench.gadget->SetDevice(discB);

    // Stage 1 -- ejected: the drive reports NO_MEDIUM.
    // TEST UNIT READY -> CHECK CONDITION (sense 02/3a/00).
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    // GESN in NO_MEDIUM reports a Media Removal event (eventCode 0x03).
    {
        auto r = PollMediaEvent(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        const u8 removal[8] = {0x00, 0x06, 0x04, 0xDE, 0x03, 0x00, 0x00, 0x00};
        CHECK_BYTES(r.data.data(), r.data.size(), removal, sizeof(removal));
    }

    // Stage 2 -- new medium present with UNIT ATTENTION after the settle window.
    SettleDiscSwap(bench);

    // TEST UNIT READY now reports the unit attention (fails).
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 1);
    // READ(10) is blocked while the UA is pending: data phase stalled, failing
    // CSW, full residue -- the media-changed signal the host relies on.
    {
        auto r = Read10LBA0(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 1);
        CHECK(r.stalledIn);
        CHECK_EQ(r.csw.dCSWDataResidue, 2048u);
        CHECK_EQ(r.data.size(), (size_t)0);
    }
    // GESN now reports NewMedia (and consumes the discChanged latch).
    {
        auto r = PollMediaEvent(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        const u8 newmedia[8] = {0x00, 0x06, 0x04, 0xDE, 0x02, 0x02, 0x00, 0x00};
        CHECK_BYTES(r.data.data(), r.data.size(), newmedia, sizeof(newmedia));
    }

    // Stage 3 -- host consumes the UA with REQUEST SENSE: reported sense is
    // 06/28/00 and the drive transitions to READY.
    {
        auto r = bench.RequestSense();
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK(r.data.size() >= 14);
        CHECK_EQ(r.data[2], 0x06);  // sense key: UNIT ATTENTION
        CHECK_EQ(r.data[12], 0x28); // ASC: NOT READY TO READY, MEDIUM MAY HAVE CHANGED
        CHECK_EQ(r.data[13], 0x00); // ASCQ
    }
    // TEST UNIT READY is now GOOD.
    CHECK_EQ(TestUnitReady(bench).csw.bmCSWStatus, 0);

    // --- The payoff: capacity and data reflect disc B, not disc A. ---
    // Disc B has 1500 sectors: last LBA = leadout - 1 = 1499 = 0x000005DB,
    // block length 2048 = 0x00000800, both big-endian.
    {
        const u8 cap[10] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        auto r = bench.SendCommand(cap, sizeof(cap), 8);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        const u8 expected[8] = {0x00, 0x00, 0x05, 0xDB, 0x00, 0x00, 0x08, 0x00};
        CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
    }
    // And READ(10) of LBA 0 now succeeds against the new disc.
    {
        auto r = Read10LBA0(bench);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK_EQ(r.data.size(), (size_t)2048);
    }
}

// GESN with the polled bit clear is an asynchronous-notification request,
// which this drive does not support: it must CHECK CONDITION with sense
// 05/24/00 (INVALID FIELD IN CDB) rather than silently returning an event.
TEST(gesn_async_notification_unsupported)
{
    CFakeImageDevice *disc = MakeDataISO(500);
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x4A, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 8);
    CHECK_EQ(r.csw.bmCSWStatus, 1);

    auto s = bench.RequestSense();
    CHECK(s.data.size() >= 14);
    CHECK_EQ(s.data[2], 0x05);  // ILLEGAL REQUEST
    CHECK_EQ(s.data[12], 0x24); // INVALID FIELD IN CDB
    CHECK_EQ(s.data[13], 0x00);
}
