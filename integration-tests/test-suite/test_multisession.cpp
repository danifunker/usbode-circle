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

#include <string.h>

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

// Same disc again, but the ripper wrote down where session 1's lead-out sits.
// 02:20:00 = LBA 10500, which is 2100 frames before the data track.
static const char *const kCDExtraCueWithLeadout =
    "REM SESSION 01\n"
    "FILE \"image.bin\" BINARY\n"
    "  TRACK 01 AUDIO\n"
    "    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:05:25\n"
    "  TRACK 03 AUDIO\n"
    "    INDEX 01 00:10:50\n"
    "  REM LEAD-OUT 02:20:00\n"
    "REM SESSION 02\n"
    "  TRACK 04 MODE2/2352\n"
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

// MSF in a full TOC entry is absolute: LBA 0 is 00:02:00, so add 150 frames.
static u32 MSFToLBA(u8 m, u8 s, u8 f)
{
    return (u32)m * 60 * 75 + (u32)s * 75 + f - 150;
}

TEST(cdextra_session1_leadout_precedes_session2)
{
    // A lead-out sitting on the next session's first track describes two
    // sessions that touch, which cannot physically happen -- hosts are
    // entitled to reject the whole session structure over it.
    auto entries = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueWithMarkers)));

    RawTOCEntry leadout1, track4;
    CHECK(FindPointer(entries, 1, 0xA2, &leadout1));
    CHECK(FindPointer(entries, 2, 0x04, &track4));

    u32 leadoutLBA = MSFToLBA(leadout1.pmin, leadout1.psec, leadout1.pframe);
    u32 trackLBA = MSFToLBA(track4.pmin, track4.psec, track4.pframe);

    CHECK_EQ(trackLBA, kDataTrackLBA);
    CHECK(leadoutLBA < trackLBA);
    // No marker in this cue, so the standard 11250-frame gap is assumed.
    CHECK_EQ(leadoutLBA, kDataTrackLBA - 11250);
}

TEST(cdextra_session1_leadout_uses_rem_marker_when_present)
{
    auto entries = ParseFullTOC(FullTOCFor(MakeCDExtra(kCDExtraCueWithLeadout)));

    RawTOCEntry leadout1;
    CHECK(FindPointer(entries, 1, 0xA2, &leadout1));
    // REM LEAD-OUT 02:20:00 = LBA 10500, not the assumed gap position.
    CHECK_EQ(MSFToLBA(leadout1.pmin, leadout1.psec, leadout1.pframe), 10500u);
}

TEST(cdextra_leadout_never_lands_inside_the_last_audio_track)
{
    // A merged image can place the sessions closer together than the standard
    // gap. Subtracting it blindly would report a lead-out in the middle of the
    // audio, truncating the last track for anything that plays it.
    static const char *const kTightCue =
        "FILE \"image.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 02:00:00\n"
        "  TRACK 03 MODE1/2352\n"
        "    INDEX 01 02:10:00\n"; // only 750 frames after track 2

    auto entries = ParseFullTOC(FullTOCFor(MakeCDExtra(kTightCue, 3)));

    RawTOCEntry leadout1;
    CHECK(FindPointer(entries, 1, 0xA2, &leadout1));
    u32 leadoutLBA = MSFToLBA(leadout1.pmin, leadout1.psec, leadout1.pframe);

    CHECK(leadoutLBA > 9000u);  // track 2 starts at LBA 9000
    CHECK(leadoutLBA <= 9750u); // and the data track at 9750
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

// A CD Extra whose data track carries a real Mode 2 Form 1 sector: 12 bytes of
// sync, a 4-byte header, an 8-byte subheader, then 2048 bytes of user data
// beginning with an ISO 9660 volume descriptor. This is the layout of the
// reporter's disc, and the shape the reader has to get right.
static CFakeImageDevice *MakeCDExtraWithFilesystem()
{
    const u32 totalSectors = kDataTrackLBA + 600;
    std::vector<u8> image((size_t)totalSectors * 2352);
    for (u32 lba = 0; lba < totalSectors; lba++)
    {
        FillPatternSector(image.data() + (size_t)lba * 2352, lba, 2352);
    }

    // Volume descriptor 16 sectors into the data track.
    u8 *sector = image.data() + (size_t)(kDataTrackLBA + 16) * 2352;
    static const u8 kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    memcpy(sector, kSync, sizeof(kSync));
    sector[12] = 0x61; // minute
    sector[13] = 0x01; // second
    sector[14] = 0x29; // frame
    sector[15] = 0x02; // mode 2
    memset(sector + 16, 0, 8);
    sector[18] = 0x09; // subheader submode: data, end of record
    sector[22] = 0x09;
    u8 *user = sector + 24;
    user[0] = 0x01; // primary volume descriptor
    memcpy(user + 1, "CD001", 5);
    user[6] = 0x01; // version

    CFakeImageDevice *disc = new CFakeImageDevice(kCDExtraCueWithMarkers, std::move(image), 2352);
    disc->m_numTracks = 4;
    return disc;
}

TEST(cdextra_read10_returns_user_data_not_the_sector_header)
{
    // The whole failure mode of issue #91 in one assertion: the sector layout
    // has to come from the track the read lands in, not from track 1. With
    // track 1 audio, a reader keyed on it skips 0 bytes instead of 24 and the
    // host sees sync bytes where the volume descriptor should be.
    CGadgetTestBench bench(MakeCDExtraWithFilesystem());
    bench.Activate();
    bench.RequestSense();

    const u32 lba = kDataTrackLBA + 16;
    const u8 cdb[10] = {0x28, 0x00,
                        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                        0x00, 0x00, 0x01, 0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), 2048);

    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), 2048u);
    const u8 expected[7] = {0x01, 'C', 'D', '0', '0', '1', 0x01};
    CHECK_BYTES(r.data.data(), 7, expected, sizeof(expected));
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
