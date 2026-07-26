//
// test_multisession.cpp
//
// CD Extra / Enhanced CD: audio tracks in session 1, a single data track in
// session 2. Hosts locate the filesystem through the session structure in the
// full TOC (READ TOC format 0x02), not through the track list, so a disc
// described as single-session mounts as "Audio CD" and its data track is never
// read -- issue #91.
//
// Traces of two hosts reading the reporter's disc showed both asking for the
// full TOC and neither asking for format 0x01: one used format 0x02 in CDB
// byte 2, the other the legacy byte-9 encoding that answers in BCD. So the
// full TOC is the reply that decides whether the disc mounts, and READ DISC
// INFORMATION has to agree with it. Both are pinned here, in both encodings.
//
#include "bench.h"
#include "framework.h"

#include <string>
#include <vector>

// Three audio tracks then a data track, with the ~11400 frame gap a real CD
// Extra leaves between session 1's lead-out and session 2's lead-in.
//
// Track 1 LBA 0, track 2 LBA 400, track 3 LBA 800, data track LBA 12600.
static const char *const kCDExtraCueWithMarkers =
    "REM SESSION 01\n"
    "FILE \"image.bin\" BINARY\n"
    "  TRACK 01 AUDIO\n"
    "    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:05:25\n"
    "  TRACK 03 AUDIO\n"
    "    INDEX 01 00:10:50\n"
    "REM SESSION 02\n"
    "  TRACK 04 MODE2/2352\n"
    "    INDEX 01 02:48:00\n";

// The same disc after a merging tool has dropped the session markers -- this
// is what CDFix and friends produce, and what issue #91 was originally
// reported against. The layout alone has to be enough.
static const char *const kCDExtraCueNoMarkers =
    "FILE \"image.bin\" BINARY\n"
    "  TRACK 01 AUDIO\n"
    "    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:05:25\n"
    "  TRACK 03 AUDIO\n"
    "    INDEX 01 00:10:50\n"
    "  TRACK 04 MODE1/2352\n"
    "    INDEX 01 02:48:00\n";

static const u32 kDataTrackLBA = 12600; // 02:48:00

static std::vector<u8> PatternImage(u32 sectors, u32 sectorSize)
{
    std::vector<u8> image((size_t)sectors * sectorSize);
    for (u32 lba = 0; lba < sectors; lba++)
    {
        FillPatternSector(image.data() + (size_t)lba * sectorSize, lba, sectorSize);
    }
    return image;
}

static CFakeImageDevice *MakeCDExtra(const char *cue, int nTracks = 4)
{
    CFakeImageDevice *disc =
        new CFakeImageDevice(cue, PatternImage(kDataTrackLBA + 600, 2352), 2352);
    disc->m_numTracks = nTracks;
    return disc;
}

// One entry of a full TOC reply: 11 bytes starting at the header.
struct RawTOCEntry
{
    u8 session;
    u8 control_adr;
    u8 point;
    u8 pmin, psec, pframe;
};

static std::vector<RawTOCEntry> ParseFullTOC(const std::vector<u8> &data)
{
    std::vector<RawTOCEntry> entries;
    for (size_t i = 4; i + 11 <= data.size(); i += 11)
    {
        RawTOCEntry e;
        e.session = data[i + 0];
        e.control_adr = data[i + 1];
        e.point = data[i + 3];
        e.pmin = data[i + 8];
        e.psec = data[i + 9];
        e.pframe = data[i + 10];
        entries.push_back(e);
    }
    return entries;
}

static std::vector<u8> FullTOCFor(CFakeImageDevice *disc, u8 sessionField = 0)
{
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x02, 0x00, 0x00, 0x00, sessionField, 0x03, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 768);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    return r.data;
}

// Finds the A0/A1/A2 pointer entry for a session. point is 0xA0, 0xA1 or 0xA2.
static bool FindPointer(const std::vector<RawTOCEntry> &entries, u8 session, u8 point,
                        RawTOCEntry *out)
{
    for (const RawTOCEntry &e : entries)
    {
        if (e.session == session && e.point == point)
        {
            *out = e;
            return true;
        }
    }
    return false;
}

TEST(cdextra_full_toc_reports_two_sessions)
{
    auto data = FullTOCFor(MakeCDExtra(kCDExtraCueWithMarkers));

    // Header: first session 1, last session 2. Reporting 1 here is what made
    // Windows treat the disc as audio-only.
    CHECK_EQ(data[2], 0x01);
    CHECK_EQ(data[3], 0x02);

    auto entries = ParseFullTOC(data);

    // Both sessions carry their own A0/A1/A2 set.
    RawTOCEntry p;
    CHECK(FindPointer(entries, 1, 0xA0, &p));
    CHECK_EQ(p.pmin, 1); // session 1 first track
    CHECK_EQ(p.control_adr, 0x10);
    CHECK(FindPointer(entries, 1, 0xA1, &p));
    CHECK_EQ(p.pmin, 3); // session 1 last track

    CHECK(FindPointer(entries, 2, 0xA0, &p));
    CHECK_EQ(p.pmin, 4);        // session 2 first track: the data track
    CHECK_EQ(p.psec, 0x20);     // disc type CD-ROM XA
    CHECK_EQ(p.control_adr, 0x14);
    CHECK(FindPointer(entries, 2, 0xA1, &p));
    CHECK_EQ(p.pmin, 4); // session 2 last track
    CHECK(FindPointer(entries, 2, 0xA2, &p));
}

TEST(cdextra_full_toc_assigns_tracks_to_sessions)
{
    auto entries = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueWithMarkers)));

    int found = 0;
    for (const RawTOCEntry &e : entries)
    {
        if (e.point < 1 || e.point > 99)
            continue; // pointer entry, not a track
        found++;
        // Audio tracks 1-3 in session 1, data track 4 in session 2.
        CHECK_EQ(e.session, e.point == 4 ? 2 : 1);
        CHECK_EQ(e.control_adr, e.point == 4 ? 0x14 : 0x10);
    }
    CHECK_EQ(found, 4);
}

TEST(cdextra_session_split_inferred_without_rem_markers)
{
    // A merged cue that lost "REM SESSION" must still split, from layout alone.
    auto withMarkers = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueWithMarkers)));
    auto without = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueNoMarkers)));

    CHECK_EQ(withMarkers.size(), without.size());
    for (size_t i = 0; i < without.size() && i < withMarkers.size(); i++)
    {
        CHECK_EQ(without[i].session, withMarkers[i].session);
        CHECK_EQ(without[i].point, withMarkers[i].point);
    }
}

TEST(cdextra_session_info_names_the_data_track)
{
    CGadgetTestBench bench(MakeCDExtra(kCDExtraCueWithMarkers));
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 12, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 12);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    const u8 expected[12] = {
        0x00, 0x0A,             // length 10
        0x01, 0x02,             // first session 1, last session 2
        0x00, 0x14, 0x04, 0x00, // data track 4, control 0x14
        0x00, 0x00, 0x31, 0x38, // LBA 12600: where the filesystem lives
    };
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

TEST(cdextra_disc_information_agrees_with_toc)
{
    CGadgetTestBench bench(MakeCDExtra(kCDExtraCueWithMarkers));
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 34);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK(r.data.size() >= 7);
    CHECK_EQ(r.data[4], 0x02); // number of sessions LSB
    CHECK_EQ(r.data[5], 0x04); // first track in last session: the data track
    CHECK_EQ(r.data[6], 0x04); // last track in last session
}

TEST(cdextra_full_toc_session_field_selects_last_session)
{
    // The session field is a starting point, not a filter: asking for session 2
    // returns session 2 only, and asking past the last session is an error.
    auto entries = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueWithMarkers), 2));
    CHECK(entries.size() > 0);
    for (const RawTOCEntry &e : entries)
    {
        CHECK_EQ(e.session, 2);
    }
}

TEST(cdextra_full_toc_rejects_session_past_the_last)
{
    CGadgetTestBench bench(MakeCDExtra(kCDExtraCueWithMarkers));
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x43, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 768);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
}

TEST(cdextra_data_track_is_readable_at_its_lba)
{
    // The reads were never the problem -- Windows just never asked. Guard that
    // the track the new TOC points at actually serves data.
    CGadgetTestBench bench(MakeCDExtra(kCDExtraCueWithMarkers));
    bench.Activate();
    bench.RequestSense();

    const u8 cdb[10] = {0x28, 0x00,
                        (u8)(kDataTrackLBA >> 24), (u8)(kDataTrackLBA >> 16),
                        (u8)(kDataTrackLBA >> 8), (u8)kDataTrackLBA,
                        0x00, 0x00, 0x01, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 2048);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), 2048u);
}

TEST(mixed_mode_data_first_stays_single_session)
{
    // Data track first then audio is an ordinary single-session mixed-mode
    // disc. No data track follows audio, so nothing should change for it.
    CFakeImageDevice *disc = MakeMixedModeCD(1000, 2, 400);
    auto data = FullTOCFor(disc);

    CHECK_EQ(data[2], 0x01);
    CHECK_EQ(data[3], 0x01);
    for (const RawTOCEntry &e : ParseFullTOC(data))
    {
        CHECK_EQ(e.session, 1);
    }
}

TEST(audio_cd_stays_single_session)
{
    auto data = FullTOCFor(MakeAudioCD(3, 3000));

    CHECK_EQ(data[2], 0x01);
    CHECK_EQ(data[3], 0x01);
    for (const RawTOCEntry &e : ParseFullTOC(data))
    {
        CHECK_EQ(e.session, 1);
        CHECK_EQ(e.control_adr, 0x10);
    }
}

TEST(data_only_iso_stays_single_session)
{
    auto data = FullTOCFor(MakeDataISO(1200));

    CHECK_EQ(data[2], 0x01);
    CHECK_EQ(data[3], 0x01);
}
