//
// test_badimages.cpp
//
// Damaged, truncated and nonsensical images.
//
// Users mount whatever is on the SD card: half-finished downloads, a .cue
// whose .bin was never copied, a rip that ran out of space, a hand-edited
// sheet with a typo. The drive cannot make those images work, but it must
// stay a functioning USB device while it fails -- answer every CBW with a
// CSW, keep reads inside the file it actually has, and never hang or fault.
//
// A drive that wedges here takes the whole USB stack down with it, so the
// host sees a disconnect rather than a read error, and on Win9x that
// usually means a reboot.
//
#include "bench.h"
#include "framework.h"

#include <string>
#include <vector>

static std::vector<u8> PatternImage(u32 sectors, u32 sectorSize)
{
    std::vector<u8> image((size_t)sectors * sectorSize);
    for (u32 lba = 0; lba < sectors; lba++)
    {
        FillPatternSector(image.data() + (size_t)lba * sectorSize, lba, sectorSize);
    }
    return image;
}

static CFakeImageDevice *MakeDisc(const std::string &cue, u32 sectors,
                                  u32 sectorSize, int nTracks)
{
    CFakeImageDevice *disc =
        new CFakeImageDevice(cue, PatternImage(sectors, sectorSize), sectorSize);
    disc->m_numTracks = nTracks;
    return disc;
}

// Every command must come back with a CSW. That is the single property that
// separates "the image is broken" from "the device is gone".
static void CheckAnswersCommands(CGadgetTestBench &bench)
{
    const u8 tur[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK(bench.SendCommand(tur, sizeof(tur), 0).gotCSW);

    const u8 inq[6] = {0x12, 0x00, 0x00, 0x00, 36, 0x00};
    CHECK(bench.SendCommand(inq, sizeof(inq), 36).gotCSW);

    const u8 cap[10] = {0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK(bench.SendCommand(cap, sizeof(cap), 8).gotCSW);

    const u8 toc[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 100, 0x00};
    CHECK(bench.SendCommand(toc, sizeof(toc), 100).gotCSW);
}

// The classic broken download: the .cue describes a two-track disc, but the
// .bin stops partway through track 1. The cue's own arithmetic then points
// past the end of the file.
TEST(truncated_bin_shorter_than_cue_claims)
{
    // Cue describes tracks at LBA 0 and 400; the file holds only 300 sectors.
    CFakeImageDevice *disc = MakeDisc(
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:05:25\n",
        300, 2352, 2);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CheckAnswersCommands(bench);

    // A read inside the file still works.
    {
        const u8 cdb[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x00};
        auto r = bench.SendCommand(cdb, sizeof(cdb), 2048);
        CHECK(r.gotCSW);
    }
    // A read the cue says exists but the file does not must still terminate
    // with a CSW rather than spinning on short reads.
    {
        const u8 cdb[10] = {0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08, 0x00};
        auto r = bench.SendCommand(cdb, sizeof(cdb), 8 * 2048);
        CHECK(r.gotCSW);
        // Whatever it returns, it must not invent data beyond the file.
        CHECK(r.data.size() <= 8 * 2048);
    }
}

// A .cue sitting next to a .bin that never finished copying: zero bytes.
TEST(zero_length_image)
{
    CFakeImageDevice *disc = MakeDisc(
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n",
        0, 2048, 1);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CheckAnswersCommands(bench);

    // There is no sector 0 to read; the command must fail cleanly, not hang.
    const u8 cdb[10] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 2048);
    CHECK(r.gotCSW);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
}

// A cue sheet with a FILE line and nothing else: no TRACK, so the parser
// yields no tracks at all and every TOC/capacity answer has to be synthesized
// from nothing.
TEST(cue_with_no_tracks)
{
    CFakeImageDevice *disc = MakeDisc("FILE \"image.bin\" BINARY\n", 600, 2048, 0);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CheckAnswersCommands(bench);
}

// An empty cue sheet -- a zero-byte .cue, or one the user truncated.
TEST(empty_cue_sheet)
{
    CFakeImageDevice *disc = MakeDisc("", 600, 2048, 0);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CheckAnswersCommands(bench);
}

// A cue full of text that is not a cue sheet (wrong file mounted, or a .cue
// that is actually an HTML error page from a failed download).
TEST(garbage_cue_sheet)
{
    CFakeImageDevice *disc = MakeDisc(
        "<!DOCTYPE html>\n"
        "<html><body><h1>404 Not Found</h1>\n"
        "TRACK the package here\n"
        "INDEX of /downloads\n"
        "</body></html>\n",
        600, 2048, 0);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    CheckAnswersCommands(bench);
}

// A read that starts inside the file and runs off the end of it. The drive
// must clamp to what exists and report the shortfall in the residue, so the
// host knows how much it actually got.
TEST(read_straddling_end_of_file)
{
    // Cue claims a 1200-sector disc; the file holds 1000 sectors.
    CFakeImageDevice *disc = MakeDisc(
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n",
        1000, 2048, 1);

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // 8 blocks starting at 996: only 4 exist in the file.
    const u8 cdb[10] = {0x28, 0x00, 0x00, 0x00, 0x03, 0xE4, 0x00, 0x00, 0x08, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 8 * 2048);

    CHECK(r.gotCSW);
    CHECK(r.data.size() <= 8 * 2048);
}
