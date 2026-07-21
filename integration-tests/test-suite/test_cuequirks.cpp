//
// test_cuequirks.cpp
//
// Real-world cue-sheet variation, driven through the real CUEParser and the
// real TOC builder.
//
// Cue sheets are the one input USBODE takes straight from the user. They are
// produced by a dozen different rippers across thirty years, so they arrive
// with CRLF endings, lowercase keywords, tabs, metadata lines, and pregaps.
// None of that is supposed to change the disc layout the host is told about.
//
// The oracle for the cosmetic cases is equality with the canonical sheet: a
// quirky cue and a clean cue describing the same disc must produce byte-for-
// byte identical TOCs. That is stronger than checking specific bytes, because
// it cannot be satisfied by a parser that mangles both sheets the same way --
// the canonical TOC is separately pinned by test_readtoc.cpp.
//
#include "bench.h"
#include "framework.h"

#include <cueparser/cueparser.h>

#include <string>
#include <vector>

// Track starts for the disc all the cosmetic cases describe: three 400-sector
// audio tracks at LBA 0 / 400 / 800 (00:00:00, 00:05:25, 00:10:50).
static const char *const kCanonicalCue =
    "FILE \"image.bin\" BINARY\n"
    "  TRACK 01 AUDIO\n"
    "    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:05:25\n"
    "  TRACK 03 AUDIO\n"
    "    INDEX 01 00:10:50\n";

static std::vector<u8> PatternImage(u32 sectors, u32 sectorSize)
{
    std::vector<u8> image((size_t)sectors * sectorSize);
    for (u32 lba = 0; lba < sectors; lba++)
    {
        FillPatternSector(image.data() + (size_t)lba * sectorSize, lba, sectorSize);
    }
    return image;
}

// Build a disc backed by the given cue sheet and return its READ TOC
// (format 0, LBA addressing) response bytes.
static std::vector<u8> TocFor(const std::string &cue, int nTracks,
                              u32 totalSectors = 1200, u32 sectorSize = 2352)
{
    CFakeImageDevice *disc =
        new CFakeImageDevice(cue, PatternImage(totalSectors, sectorSize), sectorSize);
    disc->m_numTracks = nTracks;

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 100, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 100);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    return r.data;
}

static void CheckSameAsCanonical(const std::string &cue)
{
    std::vector<u8> expected = TocFor(kCanonicalCue, 3);
    std::vector<u8> actual = TocFor(cue, 3);
    CHECK_BYTES(actual.data(), actual.size(), expected.data(), expected.size());
}

// Cue sheets authored on Windows -- which is most of them -- have CRLF line
// endings. A parser that only skips '\n' leaves a '\r' at the head of the
// next line, so the keyword compare misses and whole tracks vanish.
TEST(cue_crlf_line_endings)
{
    CheckSameAsCanonical(
        "FILE \"image.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 01 00:05:25\r\n"
        "  TRACK 03 AUDIO\r\n"
        "    INDEX 01 00:10:50\r\n");
}

// Keywords are case-insensitive in practice; several rippers emit lowercase.
TEST(cue_lowercase_keywords)
{
    CheckSameAsCanonical(
        "file \"image.bin\" binary\n"
        "  track 01 audio\n"
        "    index 01 00:00:00\n"
        "  Track 02 Audio\n"
        "    Index 01 00:05:25\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:10:50\n");
}

// Indentation is decorative: tabs, ragged spacing, blank lines between
// tracks, and a missing newline on the final line are all common.
TEST(cue_irregular_whitespace)
{
    CheckSameAsCanonical(
        "FILE \"image.bin\" BINARY\n"
        "\n"
        "\tTRACK 01 AUDIO\n"
        "\t\tINDEX 01 00:00:00\n"
        "\n"
        "TRACK 02 AUDIO\n"
        "INDEX 01 00:05:25\n"
        "\n"
        "        TRACK 03 AUDIO\n"
        "          INDEX 01 00:10:50"); // no trailing newline
}

// Metadata and flag lines carry no layout information and must be skipped,
// not misread. REM lines in particular can contain anything, including words
// the parser cares about elsewhere.
TEST(cue_metadata_lines_ignored)
{
    CheckSameAsCanonical(
        "REM GENRE Soundtrack\n"
        "REM DATE 1995\n"
        "REM COMMENT \"ExactAudioCopy v1.0\"\n"
        "CATALOG 0000000000000\n"
        "PERFORMER \"Some Artist\"\n"
        "TITLE \"Some Album\"\n"
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Track One\"\n"
        "    PERFORMER \"Some Artist\"\n"
        "    ISRC ABCDE1234567\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    FLAGS DCP\n"
        "    SONGWRITER \"Someone\"\n"
        "    INDEX 01 00:05:25\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:10:50\n");
}

// Filenames routinely contain spaces, and rippers often prefix "./". Neither
// affects the TOC, but a quote-handling slip truncates the name at the first
// space, which on hardware means the .bin is never found at all.
TEST(cue_filename_with_spaces_and_dot_slash)
{
    CheckSameAsCanonical(
        "FILE \"./My Game Disc (1995).bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:05:25\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:10:50\n");
}

// A stored pregap (INDEX 00) is silence that exists in the file as well as on
// the disc. INDEX 01 is where the track's audio actually starts, and that is
// what the TOC must report -- reporting INDEX 00 instead starts every track a
// couple of seconds early, into the gap.
TEST(cue_stored_pregap_index00_not_reported_as_track_start)
{
    CheckSameAsCanonical(
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:05:00\n" // 375: gap start, present in the file
        "    INDEX 01 00:05:25\n" // 400: audio start -- the TOC address
        "  TRACK 03 AUDIO\n"
        "    INDEX 00 00:10:25\n" // 775
        "    INDEX 01 00:10:50\n" // 800
        "\n");
}

// An unstored PREGAP is silence the drive generates: it occupies disc
// addresses but no bytes in the file, so every following track sits later on
// the disc than its INDEX time alone suggests. Getting this wrong shifts the
// whole back half of the disc.
TEST(cue_unstored_pregap_shifts_following_tracks)
{
    // 150 frames (2 s) of unstored pregap before track 2. Track 2 therefore
    // starts at 400 + 150 = 550, and track 3 carries the same accumulated
    // offset: 800 + 150 = 950. The leadout moves with them: the file still
    // holds 1200 sectors, but the disc is 150 frames longer than the file
    // because that silence is generated rather than stored, so the leadout
    // is at 950 + (1200 - 800) = 1350.
    std::vector<u8> toc = TocFor(
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 00:05:25\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:10:50\n",
        3);

    const u8 expected[36] = {
        0x00, 0x22,             // TOC length = 34
        0x01, 0x03,             // first track 1, last track 3
        0x00, 0x10, 0x01, 0x00, // track 1: audio, ADR 1
        0x00, 0x00, 0x00, 0x00, // LBA 0
        0x00, 0x10, 0x02, 0x00, // track 2
        0x00, 0x00, 0x02, 0x26, // LBA 550
        0x00, 0x10, 0x03, 0x00, // track 3
        0x00, 0x00, 0x03, 0xB6, // LBA 950
        0x00, 0x10, 0xAA, 0x00, // leadout
        0x00, 0x00, 0x05, 0x46, // LBA 1350 = 950 + 400 remaining file sectors
    };
    CHECK_BYTES(toc.data(), toc.size(), expected, sizeof(expected));
}

// The shape every redump-style split rip has: one FILE per track, with the
// audio track's pregap stored in its own file. The parser derives each file's
// start from a prev_file_size argument that no caller in the firmware passes,
// so once a stored INDEX 00 has advanced file_offset the unsigned subtraction
// underflows: track 3 came back at LBA 2308179703 instead of 150.
//
// Asserted against the parser directly rather than through a TOC, because
// this is parser arithmetic and the numbers are exact.
//
// The parser cannot place these tracks correctly without the real file sizes,
// and the loader does not have them (it ignores the FILE names and always
// opens the bin named after the cue). So the guarantee here is the one that
// can be met today: every track lands somewhere sane on the disc.
TEST(multifile_cue_does_not_underflow_into_a_nonsense_lba)
{
    const char *cue =
        "FILE \"Game (Track 1).bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"Game (Track 2).bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:00:00\n"
        "    INDEX 01 00:02:00\n"
        "FILE \"Game (Track 3).bin\" BINARY\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    CUEParser parser(cue);
    const CUETrackInfo *t;
    int seen = 0;
    while ((t = parser.next_track()) != nullptr)
    {
        seen++;
        // A CD holds 74 to 80 minutes, so about 360000 sectors. Anything past
        // that is arithmetic gone wrong, not a real address.
        CHECK(t->track_start < 400000);
        CHECK(t->data_start < 400000);
        CHECK(t->file_start < 400000);
    }
    CHECK_EQ(seen, 3);

    // Track 3 follows track 2's 150-sector pregap rather than restarting at 0.
    parser.restart();
    parser.next_track();
    parser.next_track();
    const CUETrackInfo *third = parser.next_track();
    CHECK_EQ(third->track_number, 3);
    CHECK_EQ(third->track_start, 150u);
}
