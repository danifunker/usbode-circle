//
// test_mdsimages.cpp
//
// MDS/MDF (Alcohol 120%) images: the one disc format the suite did not
// cover. Compiles and drives the real firmware readers - addon/mdsparser/
// mdsparser.cpp and addon/discimage/mdsfile.cpp - the same way
// test_realimages.cpp drives the CUE/BIN and CHD readers, right down to
// READ(10) on the wire through CGadgetTestBench.
//
// There is no MDS fixture to ship: Alcohol 120% is proprietary and the
// images people actually mount are game discs. So the .mds files here are
// authored by the test out of the format's own on-disk structures. Two
// things keep that from being circular:
//
//   * the structure sizes are pinned against the published MDS layout
//     (88/24/80/8/16 bytes). A change to mdsparser.h that would misparse a
//     real Alcohol image fails here even though the test writes its own.
//   * the MDF payload is real. Data tracks hold raw 2352-byte MODE1 sectors
//     wrapping the tracked FreeDOS ISO, so a read landing at the wrong
//     offset is caught by ISO9660's own volume descriptors rather than by a
//     pattern this file also generates.
//
// The lead-in entries (points 0xA0/0xA1/0xA2) that real MDS files carry are
// written with a zero sector_size and a bogus start sector on purpose: they
// must be skipped, and if the skip ever regresses the arithmetic downstream
// goes visibly wrong instead of quietly working.
//
#include "bench.h"
#include "framework.h"

#include <discimage/mdsfile.h>
#include <fatfs/ff.h>
#include <mdsparser/mdsparser.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

// The on-disk MDS layout, as documented by the format's reverse engineers
// (and as libmirage/cdemu implement it). These are what a file written by
// Alcohol 120% actually contains; asserting them here means the structs in
// mdsparser.h cannot drift away from real images unnoticed.
static_assert(sizeof(MDS_Header) == 88, "MDS header is 88 bytes on disc");
static_assert(sizeof(MDS_SessionBlock) == 24, "MDS session block is 24 bytes on disc");
static_assert(sizeof(MDS_TrackBlock) == 80, "MDS track block is 80 bytes on disc");
static_assert(sizeof(MDS_TrackExtraBlock) == 8, "MDS track extra block is 8 bytes on disc");
static_assert(sizeof(MDS_Footer) == 16, "MDS footer is 16 bytes on disc");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TestDataDir()
{
#ifdef USBODE_TESTDATA
    return USBODE_TESTDATA;
#else
    return "out/images";
#endif
}

static u64 FileSize(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return (u64)st.st_size;
}

// Deterministic bytes for the parts of an image that are not real ISO
// content (audio sectors, subchannel areas), so they can be checked exactly.
static u8 AudioByte(u64 fileOffset)
{
    return (u8)(fileOffset * 31u + 7u);
}

static u8 SubchannelByte(u32 lba, u32 i)
{
    return (u8)(lba * 13u + i * 7u + 3u);
}

// One track block as it will be written into the .mds.
struct MdsTrackSpec
{
    u8 mode = 0xAA;      // 0xA9 = audio, 0xAA = MODE1 (what the reader tests for)
    u8 subchannel = 0;   // 0 = none, 0x08 = P-W subchannels appended per sector
    u8 point = 1;        // track number
    u16 sectorSize = 2352;
    u32 startSector = 0; // absolute LBA on the disc
    u64 startOffset = 0; // byte offset of that sector in the MDF
    u32 pregap = 150;
    u32 length = 0;      // sectors
};

// Serialize a .mds the way Alcohol 120% lays one out: header, one session
// block, the track blocks (three lead-in entries first, as real files have),
// the per-track extra blocks, one footer, then the MDF filename.
static void WriteMdsFile(const std::string &path,
                         const std::vector<MdsTrackSpec> &tracks,
                         const std::string &mdfName,
                         bool wideCharName = false)
{
    const size_t nLeadIn = 3; // points 0xA0, 0xA1, 0xA2
    const size_t nBlocks = nLeadIn + tracks.size();

    const u32 sessionOff = (u32)sizeof(MDS_Header);
    const u32 trackOff = sessionOff + (u32)sizeof(MDS_SessionBlock);
    const u32 extraOff = trackOff + (u32)(nBlocks * sizeof(MDS_TrackBlock));
    const u32 footerOff = extraOff + (u32)(tracks.size() * sizeof(MDS_TrackExtraBlock));
    const u32 nameOff = footerOff + (u32)sizeof(MDS_Footer);

    std::vector<u8> name;
    if (wideCharName) {
        // UTF-16LE, as Alcohol writes when the footer's widechar flag is set.
        for (char c : mdfName) {
            name.push_back((u8)c);
            name.push_back(0);
        }
        name.push_back(0);
        name.push_back(0);
    } else {
        name.assign(mdfName.begin(), mdfName.end());
        name.push_back(0);
    }

    std::vector<u8> buf(nameOff + name.size(), 0);

    u32 leadout = 0;
    for (const MdsTrackSpec &t : tracks) {
        if (t.startSector + t.length > leadout) {
            leadout = t.startSector + t.length;
        }
    }

    MDS_Header hdr{};
    memcpy(hdr.signature, "MEDIA DESCRIPTOR", 16);
    hdr.version[0] = 1;
    hdr.version[1] = 3;
    hdr.medium_type = 0x00; // CD-ROM
    hdr.num_sessions = 1;
    hdr.sessions_blocks_offset = sessionOff;
    memcpy(buf.data(), &hdr, sizeof(hdr));

    MDS_SessionBlock ses{};
    ses.session_start = -150;
    ses.session_end = (int32_t)leadout;
    ses.session_number = 1;
    ses.num_all_blocks = (u8)nBlocks;
    ses.num_nontrack_blocks = (u8)nLeadIn;
    ses.first_track = tracks.empty() ? 0 : tracks.front().point;
    ses.last_track = tracks.empty() ? 0 : tracks.back().point;
    ses.tracks_blocks_offset = trackOff;
    memcpy(buf.data() + sessionOff, &ses, sizeof(ses));

    // Lead-in descriptors. The reader must skip anything with point >= 0xA0,
    // so these carry a zero sector size and an out-of-range start sector: if
    // the skip regresses, the offset arithmetic breaks loudly.
    const u8 leadInPoints[nLeadIn] = {0xA0, 0xA1, 0xA2};
    for (size_t i = 0; i < nLeadIn; i++) {
        MDS_TrackBlock blk{};
        blk.mode = 0xA9;
        blk.adr_ctl = 0x14;
        blk.point = leadInPoints[i];
        blk.sector_size = 0;
        blk.start_sector = 0xFFFFFF00;
        // Alcohol stores first track / last track / leadout MSF here; the
        // reader derives all three from the track blocks instead.
        if (leadInPoints[i] == 0xA0) {
            blk.pmin = tracks.empty() ? 0 : tracks.front().point;
        } else if (leadInPoints[i] == 0xA1) {
            blk.pmin = tracks.empty() ? 0 : tracks.back().point;
        } else {
            u32 amsf = leadout + 150;
            blk.pmin = (u8)(amsf / (60 * 75));
            blk.psec = (u8)((amsf / 75) % 60);
            blk.pframe = (u8)(amsf % 75);
        }
        memcpy(buf.data() + trackOff + i * sizeof(MDS_TrackBlock), &blk, sizeof(blk));
    }

    for (size_t i = 0; i < tracks.size(); i++) {
        const MdsTrackSpec &t = tracks[i];
        const u32 thisExtraOff = extraOff + (u32)(i * sizeof(MDS_TrackExtraBlock));

        MDS_TrackBlock blk{};
        blk.mode = t.mode;
        blk.subchannel = t.subchannel;
        blk.adr_ctl = (t.mode == 0xA9) ? 0x10 : 0x14; // audio vs data control bits
        blk.point = t.point;
        blk.extra_offset = thisExtraOff;
        blk.sector_size = t.sectorSize;
        blk.start_sector = t.startSector;
        blk.start_offset = t.startOffset;
        blk.number_of_files = 1;
        blk.footer_offset = footerOff;
        memcpy(buf.data() + trackOff + (nLeadIn + i) * sizeof(MDS_TrackBlock), &blk, sizeof(blk));

        MDS_TrackExtraBlock extra{};
        extra.pregap = t.pregap;
        extra.length = t.length;
        memcpy(buf.data() + thisExtraOff, &extra, sizeof(extra));
    }

    MDS_Footer footer{};
    footer.filename_offset = nameOff;
    footer.widechar_filename = wideCharName ? 1 : 0;
    memcpy(buf.data() + footerOff, &footer, sizeof(footer));

    memcpy(buf.data() + nameOff, name.data(), name.size());

    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return;
    }
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
}

// Raw 2352-byte MODE1 sectors wrapping real ISO9660 content: 12-byte sync,
// 3-byte BCD address, the mode byte, 2048 bytes of user data, then the
// EDC/ECC area (left zero - the firmware does not verify it). This is the
// sector layout an MDS data track stores, and it is what puts the ISO's own
// volume descriptors at a known LBA inside the MDF.
static std::vector<u8> RawMode1Sectors(const std::string &isoPath, u32 firstLBA, u32 nSectors)
{
    std::vector<u8> out;
    FILE *in = fopen(isoPath.c_str(), "rb");
    if (!in) {
        return out;
    }
    if (fseeko(in, (off_t)firstLBA * 2048, SEEK_SET) != 0) {
        fclose(in);
        return out;
    }

    static const u8 kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    auto bcd = [](u32 v) { return (u8)(((v / 10) << 4) | (v % 10)); };

    out.resize((size_t)nSectors * 2352, 0);
    for (u32 i = 0; i < nSectors; i++) {
        u8 *sector = out.data() + (size_t)i * 2352;
        memcpy(sector, kSync, sizeof(kSync));
        u32 amsf = firstLBA + i + 150;
        sector[12] = bcd(amsf / (60 * 75));
        sector[13] = bcd((amsf / 75) % 60);
        sector[14] = bcd(amsf % 75);
        sector[15] = 0x01; // MODE1
        if (fread(sector + 16, 1, 2048, in) != 2048) {
            fclose(in);
            out.clear();
            return out;
        }
    }
    fclose(in);
    return out;
}

static void WriteBytes(const std::string &path, const std::vector<u8> &bytes)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return;
    }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
}

// Mirrors util.cpp's loadMDSFileDevice: slurp the .mds through the FatFs
// shim, hand the bytes and their length to CMDSFileDevice, Init(). Returns
// nullptr if the device rejects the image - having destroyed it, which is
// the path a hostile or truncated .mds has to survive without taking the
// firmware down.
static CMDSFileDevice *OpenMds(const std::string &mdsPath)
{
    FIL f;
    if (f_open(&f, mdsPath.c_str(), FA_READ) != FR_OK) {
        return nullptr;
    }
    DWORD size = f_size(&f);
    char *blob = new char[size + 1];
    UINT br = 0;
    FRESULT r = f_read(&f, blob, size, &br);
    f_close(&f);
    if (r != FR_OK || br != size) {
        delete[] blob;
        return nullptr;
    }
    blob[size] = '\0';

    CMDSFileDevice *dev = new CMDSFileDevice(mdsPath.c_str(), blob, size, MEDIA_TYPE::CD);
    if (!dev->Init()) {
        delete dev; // takes the blob with it
        return nullptr;
    }
    return dev;
}

static const std::string kIso = TestDataDir() + "/freedos-test.iso";

// ---------------------------------------------------------------------------
// Format layout
// ---------------------------------------------------------------------------

TEST(mds_structures_match_the_published_on_disc_layout)
{
    // The static_asserts above are the real gate; these repeat them at run
    // time so a size mismatch is reported as a named failure rather than as
    // a compile error with no test attached.
    CHECK_EQ(sizeof(MDS_Header), (size_t)88);
    CHECK_EQ(sizeof(MDS_SessionBlock), (size_t)24);
    CHECK_EQ(sizeof(MDS_TrackBlock), (size_t)80);
    CHECK_EQ(sizeof(MDS_TrackExtraBlock), (size_t)8);
    CHECK_EQ(sizeof(MDS_Footer), (size_t)16);

    // The signature the parser matches on occupies the first 16 bytes.
    MDS_Header hdr{};
    memcpy(hdr.signature, "MEDIA DESCRIPTOR", 16);
    CHECK_EQ((size_t)((const char *)hdr.signature - (const char *)&hdr), (size_t)0);
    CHECK(memcmp(&hdr, "MEDIA DESCRIPTOR", 16) == 0);
}

// ---------------------------------------------------------------------------
// Single data track, real ISO9660 content
// ---------------------------------------------------------------------------

TEST(mds_data_disc_reads_through_the_gadget)
{
    const u32 nSectors = 32; // covers the PVD at LBA 16 and the Joliet SVD at 17
    CHECK(FileSize(kIso) > 0);

    const std::string mds = TestDataDir() + "/mdsdata.mds";
    const std::string mdf = TestDataDir() + "/mdsdata.mdf";
    std::vector<u8> raw = RawMode1Sectors(kIso, 0, nSectors);
    CHECK_EQ(raw.size(), (size_t)nSectors * 2352);
    if (raw.empty()) {
        return;
    }
    WriteBytes(mdf, raw);

    MdsTrackSpec track;
    track.mode = 0xAA;
    track.point = 1;
    track.sectorSize = 2352;
    track.startSector = 0;
    track.startOffset = 0;
    track.length = nSectors;
    WriteMdsFile(mds, {track}, "mdsdata.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CHECK(disc->GetFileType() == FileType::MDS);
    CHECK_EQ(disc->GetNumTracks(), 1);
    CHECK_EQ(disc->GetTrackStart(0), 0u);
    CHECK_EQ(disc->GetTrackLength(0), nSectors);
    CHECK(!disc->IsAudioTrack(0));
    CHECK(!disc->HasSubchannelData());

    // The CUE the reader synthesizes for the rest of the stack. A data track
    // is MODE1/2352 (raw sectors), and no PREGAP is emitted because
    // start_sector is already an absolute disc LBA.
    const char *expectedCue =
        "FILE \"mdsdata.mdf\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n";
    CHECK(disc->GetCueSheet() != nullptr);
    if (disc->GetCueSheet()) {
        CHECK(strcmp(disc->GetCueSheet(), expectedCue) == 0);
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // READ CAPACITY: 32 sectors, so the last addressable LBA is 31.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    u32 blockSize = (cap.data[4] << 24) | (cap.data[5] << 16) | (cap.data[6] << 8) | cap.data[7];
    CHECK_EQ(blockSize, 2048u);
    CHECK_EQ(lastLBA, nSectors - 1);

    // The oracle: LBA 16 of the wrapped ISO is its Primary Volume Descriptor.
    // Getting these bytes back means the whole chain is right - the MDS track
    // table, the LBA -> MDF offset mapping, and the 16-byte raw-sector header
    // skip the gadget applies for MODE1/2352.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK_EQ(pvd.data.size(), (size_t)2048);
    if (pvd.data.size() == 2048) {
        CHECK_EQ(pvd.data[0], 0x01);
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
        CHECK(memcmp(pvd.data.data() + 40, "FREEDOS_TEST", 12) == 0);
    }

    // LBA 17 is the Joliet supplementary descriptor: confirms the stride
    // between sectors, not just the base offset.
    const u8 svdCdb[10] = {0x28, 0, 0, 0, 0, 17, 0, 0, 1, 0};
    auto svd = bench.SendCommand(svdCdb, sizeof(svdCdb), 2048);
    CHECK_EQ(svd.csw.bmCSWStatus, 0);
    if (svd.data.size() == 2048) {
        CHECK_EQ(svd.data[0], 0x02);
        CHECK(memcmp(svd.data.data() + 1, "CD001", 5) == 0);
    }

    // A multi-sector read in one command, so the per-block stride inside a
    // single batch is covered as well as the starting offset.
    const u8 twoCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 2, 0};
    auto two = bench.SendCommand(twoCdb, sizeof(twoCdb), 2 * 2048);
    CHECK_EQ(two.csw.bmCSWStatus, 0);
    CHECK_EQ(two.data.size(), (size_t)(2 * 2048));
    if (two.data.size() == 2 * 2048) {
        CHECK(memcmp(two.data.data() + 1, "CD001", 5) == 0);
        CHECK_EQ(two.data[2048], 0x02); // second sector is the SVD
    }

    // Pure data disc -> medium type 0x01, one data track in the TOC.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x01);

    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, 100, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 100);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);        // first track
    CHECK_EQ(toc.data[3], 0x01);        // last track
    CHECK_EQ(toc.data[5] & 0x04, 0x04); // track 1 control: data
}

// ---------------------------------------------------------------------------
// MDF filename resolution
// ---------------------------------------------------------------------------

// Alcohol writes a literal "*.mdf" when the data file just shadows the .mds
// name. The reader has to derive the real name from the .mds path, and use
// the derived name (not the wildcard) in the CUE it hands downstream.
TEST(mds_wildcard_mdf_name_is_derived_from_the_mds_path)
{
    const u32 nSectors = 20;
    const std::string mds = TestDataDir() + "/mdswild.mds";
    const std::string mdf = TestDataDir() + "/mdswild.mdf";
    std::vector<u8> raw = RawMode1Sectors(kIso, 0, nSectors);
    if (raw.empty()) {
        CHECK(false);
        return;
    }
    WriteBytes(mdf, raw);

    MdsTrackSpec track;
    track.length = nSectors;
    WriteMdsFile(mds, {track}, "*.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr); // the wildcard must resolve to mdswild.mdf
    if (!disc) {
        return;
    }

    CHECK(disc->GetCueSheet() != nullptr);
    if (disc->GetCueSheet()) {
        // The CUE names the resolved file, with no directory prefix, and
        // never the wildcard itself.
        CHECK(strstr(disc->GetCueSheet(), "FILE \"mdswild.mdf\" BINARY") != nullptr);
        CHECK(strstr(disc->GetCueSheet(), "*") == nullptr);
    }

    // And it really opened the data: the ISO's PVD is where it should be.
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    if (pvd.data.size() == 2048) {
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
    }
}

// Alcohol stores the data filename as UTF-16 when the footer's widechar flag
// is set. The reader converts it to UTF-8 before opening the file.
TEST(mds_widechar_mdf_name_is_converted_from_utf16)
{
    const u32 nSectors = 20;
    const std::string mds = TestDataDir() + "/mdswide.mds";
    const std::string mdf = TestDataDir() + "/mdswide.mdf";
    std::vector<u8> raw = RawMode1Sectors(kIso, 0, nSectors);
    if (raw.empty()) {
        CHECK(false);
        return;
    }
    WriteBytes(mdf, raw);

    MdsTrackSpec track;
    track.length = nSectors;
    WriteMdsFile(mds, {track}, "mdswide.mdf", /*wideCharName=*/true);

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CHECK(disc->GetCueSheet() != nullptr);
    if (disc->GetCueSheet()) {
        CHECK(strstr(disc->GetCueSheet(), "FILE \"mdswide.mdf\" BINARY") != nullptr);
    }
}

// ---------------------------------------------------------------------------
// Mixed data + audio disc
// ---------------------------------------------------------------------------

TEST(mds_mixed_data_and_audio_disc)
{
    const u32 dataSectors = 100;
    const u32 audioSectors = 50;
    const u64 audioOffset = (u64)dataSectors * 2352;

    const std::string mds = TestDataDir() + "/mdsmixed.mds";
    const std::string mdf = TestDataDir() + "/mdsmixed.mdf";

    std::vector<u8> image = RawMode1Sectors(kIso, 0, dataSectors);
    if (image.empty()) {
        CHECK(false);
        return;
    }
    image.resize(image.size() + (size_t)audioSectors * 2352);
    for (u64 i = 0; i < (u64)audioSectors * 2352; i++) {
        image[(size_t)(audioOffset + i)] = AudioByte(audioOffset + i);
    }
    WriteBytes(mdf, image);

    MdsTrackSpec data;
    data.mode = 0xAA;
    data.point = 1;
    data.startSector = 0;
    data.startOffset = 0;
    data.length = dataSectors;

    MdsTrackSpec audio;
    audio.mode = 0xA9; // audio
    audio.point = 2;
    audio.startSector = dataSectors;
    audio.startOffset = audioOffset;
    audio.pregap = 0;
    audio.length = audioSectors;

    WriteMdsFile(mds, {data, audio}, "mdsmixed.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CHECK_EQ(disc->GetNumTracks(), 2);
    CHECK(!disc->IsAudioTrack(0));
    CHECK(disc->IsAudioTrack(1));
    CHECK_EQ(disc->GetTrackStart(1), dataSectors);
    CHECK_EQ(disc->GetTrackLength(1), audioSectors);

    // Track 2 is emitted as AUDIO, and its INDEX is the absolute start
    // sector converted to MSF: LBA 100 -> 00:01:25.
    CHECK(disc->GetCueSheet() != nullptr);
    if (disc->GetCueSheet()) {
        CHECK(strstr(disc->GetCueSheet(), "  TRACK 02 AUDIO\n    INDEX 01 00:01:25\n") != nullptr);
        CHECK(strstr(disc->GetCueSheet(), "PREGAP") == nullptr);
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Data + audio -> medium type 0x03.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x03);

    // TOC: two tracks, track 1 data, track 2 audio.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);
    CHECK_EQ(toc.data[3], 0x02);
    CHECK_EQ(toc.data[5] & 0x04, 0x04);  // track 1: data
    CHECK_EQ(toc.data[13] & 0x04, 0x00); // track 2: audio

    // A data sector well inside track 1, on the wire.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    if (pvd.data.size() == 2048) {
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
    }

    // Audio sectors live past the track boundary. Unlike CUE/BIN, an MDS
    // presents a uniform 2352-bytes-per-frame logical space, so the offset
    // for an audio LBA is simply lba * 2352 - but the reader still has to
    // route it through the track table to reach the right part of the MDF.
    const u32 audioLBA = dataSectors + 5;
    const u64 off = disc->GetByteOffsetForLBA(audioLBA);
    CHECK_EQ(off, (u64)audioLBA * 2352);
    CHECK_EQ(disc->Seek(off), off);
    u8 buf[2352];
    int n = disc->Read(buf, sizeof(buf));
    CHECK_EQ(n, (int)sizeof(buf));
    u8 expected[2352];
    for (u32 i = 0; i < 2352; i++) {
        expected[i] = AudioByte(off + i);
    }
    CHECK_BYTES(buf, (size_t)n, expected, sizeof(expected));
}

// ---------------------------------------------------------------------------
// Mode 2 / CD-ROM XA discs
// ---------------------------------------------------------------------------

// Every other fixture in this file is assembled here from parts. This one is
// not, and deliberately: the whole question a Mode 2 test has to answer is
// whether user data starts 24 bytes into the sector instead of 16, and a
// fixture built from the same belief the reader holds could not answer it.
// out/images/videocd-xa.bin is real sectors off a real Video CD (libcdio's
// test corpus, GPLv3 as we are) -- 32 sectors of its ISO9660 track, which is
// Mode 2 Form 1, then 24 sectors of its MPEG track, which is Form 2.
// testdata/README-testdata.md records exactly what was done to them.
static const std::string kXa = TestDataDir() + "/videocd-xa.bin";

static const u32 kXaForm1Sectors = 32;
static const u32 kXaForm2Sectors = 24;

static std::vector<u8> ReadAllBytes(const std::string &path)
{
    std::vector<u8> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return out;
    }
    fseeko(f, 0, SEEK_END);
    off_t size = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (size > 0) {
        out.resize((size_t)size);
        if (fread(out.data(), 1, out.size(), f) != out.size()) {
            out.clear();
        }
    }
    fclose(f);
    return out;
}

TEST(mds_mode2_disc_reads_user_data_from_offset_24)
{
    std::vector<u8> raw = ReadAllBytes(kXa);
    CHECK_EQ(raw.size(), (size_t)(kXaForm1Sectors + kXaForm2Sectors) * 2352);
    if (raw.empty()) {
        return;
    }

    // Guard the fixture itself before trusting it as an oracle. If the file
    // ever gets rebuilt wrong, this fails here with an obvious message instead
    // of showing up as a mysterious reader bug.
    CHECK_EQ(raw[15], 0x02);            // mode byte in the sector header
    CHECK_EQ(raw[16 + 2], raw[16 + 6]); // subheader is the duplicated 4+4 pair
    CHECK_EQ(raw[16 * 2352 + 24], 0x01);
    CHECK(memcmp(raw.data() + 16 * 2352 + 25, "CD001", 5) == 0);

    const std::string mds = TestDataDir() + "/mdsmode2.mds";
    const std::string mdf = TestDataDir() + "/mdsmode2.mdf";
    WriteBytes(mdf, raw);

    MdsTrackSpec form1;
    form1.mode = 0xAC; // Mode 2 Form 1
    form1.point = 1;
    form1.sectorSize = 2352;
    form1.startSector = 0;
    form1.startOffset = 0;
    form1.length = kXaForm1Sectors;

    MdsTrackSpec form2;
    form2.mode = 0xAD; // Mode 2 Form 2
    form2.point = 2;
    form2.sectorSize = 2352;
    form2.startSector = kXaForm1Sectors;
    form2.startOffset = (u64)kXaForm1Sectors * 2352;
    form2.length = kXaForm2Sectors;

    WriteMdsFile(mds, {form1, form2}, "mdsmode2.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CHECK_EQ(disc->GetNumTracks(), 2);
    // Mode 2 is data in both forms. Reporting Form 2 as audio would send the
    // whole MPEG track to the CD player as if it were PCM.
    CHECK(!disc->IsAudioTrack(0));
    CHECK(!disc->IsAudioTrack(1));

    // Both tracks are described MODE2/2352, which is what makes the rest of
    // the stack skip 24 bytes per sector rather than 16. Before this mapping
    // existed every non-audio track came out MODE1/2352.
    const char *expectedCue =
        "FILE \"mdsmode2.mdf\" BINARY\n"
        "  TRACK 01 MODE2/2352\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 MODE2/2352\n"
        "    INDEX 01 00:00:32\n";
    CHECK(disc->GetCueSheet() != nullptr);
    if (disc->GetCueSheet()) {
        CHECK(strcmp(disc->GetCueSheet(), expectedCue) == 0);
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    CHECK_EQ(lastLBA, kXaForm1Sectors + kXaForm2Sectors - 1);

    // The oracle, and the assertion this whole fixture exists for. The Video
    // CD's Primary Volume Descriptor is at LBA 16. It is only at offset 24 of
    // that sector; at offset 16 sits the subheader. A reader that skips 16
    // returns the subheader followed by the first 2040 bytes of user data, so
    // data[0] is a submode byte and "CD001" lands 8 bytes late.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK_EQ(pvd.data.size(), (size_t)2048);
    if (pvd.data.size() == 2048) {
        CHECK_EQ(pvd.data[0], 0x01); // volume descriptor type: primary
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
        // CD-i Bridge: the system identifier a real Video CD carries.
        CHECK(memcmp(pvd.data.data() + 8, "CD-RTOS CD-BRIDGE", 17) == 0);
        CHECK(memcmp(pvd.data.data() + 40, "SVIDEOCD", 8) == 0);
    }

    // LBA 17 is the volume descriptor set terminator, so the per-sector stride
    // is covered too and not just the offset into the first one.
    const u8 termCdb[10] = {0x28, 0, 0, 0, 0, 17, 0, 0, 1, 0};
    auto term = bench.SendCommand(termCdb, sizeof(termCdb), 2048);
    CHECK_EQ(term.csw.bmCSWStatus, 0);
    if (term.data.size() == 2048) {
        CHECK_EQ(term.data[0], 0xFF);
        CHECK(memcmp(term.data.data() + 1, "CD001", 5) == 0);
    }

    // Two sectors in one command: the stride within a single batched read.
    const u8 twoCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 2, 0};
    auto two = bench.SendCommand(twoCdb, sizeof(twoCdb), 2 * 2048);
    CHECK_EQ(two.csw.bmCSWStatus, 0);
    CHECK_EQ(two.data.size(), (size_t)(2 * 2048));
    if (two.data.size() == 2 * 2048) {
        CHECK(memcmp(two.data.data() + 1, "CD001", 5) == 0);
        CHECK_EQ(two.data[2048], 0xFF);
    }

    // The Form 2 track. Its user data starts at offset 24 as well, and these
    // are real MPEG-1 program stream sectors, so each one opens with the pack
    // start code 00 00 01 BA.
    const u8 mpegCdb[10] = {0x28, 0, 0, 0, 0, (u8)kXaForm1Sectors, 0, 0, 1, 0};
    auto mpeg = bench.SendCommand(mpegCdb, sizeof(mpegCdb), 2048);
    CHECK_EQ(mpeg.csw.bmCSWStatus, 0);
    if (mpeg.data.size() == 2048) {
        static const u8 kPackHeader[4] = {0x00, 0x00, 0x01, 0xBA};
        CHECK(memcmp(mpeg.data.data(), kPackHeader, 4) == 0);
    }

    // Both tracks report as data in the TOC.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, 100, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 100);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);        // first track
    CHECK_EQ(toc.data[3], 0x02);        // last track
    CHECK_EQ(toc.data[5] & 0x04, 0x04); // track 1 control: data
}

// The mode byte is not a single value per track type. libmirage -- the
// reference open-source MDS reader -- selects on the low nibble only, and
// every code has an alias 8 higher; the 0xA9/0xAA that Alcohol writes are the
// +8 forms of 0x01/0x02. Sweeping the table here means a disc whose writer
// used any of the recognised codes is described correctly, rather than only
// the two values that happened to be hardcoded.
TEST(mds_track_mode_bytes_map_to_the_published_types)
{
    std::vector<u8> raw = RawMode1Sectors(kIso, 0, 4);
    if (raw.empty()) {
        return;
    }
    const std::string mdf = TestDataDir() + "/mdsmodes.mdf";
    WriteBytes(mdf, raw);

    struct Case
    {
        u8 mode;
        const char *cue;
        bool audio;
    };
    const Case cases[] = {
        {0xA9, "AUDIO", true},        // audio, as Alcohol writes it
        {0x01, "AUDIO", true},        // same code without the +8 offset
        {0xAA, "MODE1/2352", false},  // Mode 1
        {0x02, "MODE1/2352", false},
        {0xA8, "MODE2/2352", false},  // Mode 2, formless
        {0x00, "MODE2/2352", false},
        {0xAB, "MODE2/2352", false},  // Mode 2
        {0x03, "MODE2/2352", false},
        {0xAC, "MODE2/2352", false},  // Mode 2 Form 1
        {0x04, "MODE2/2352", false},
        {0xAD, "MODE2/2352", false},  // Mode 2 Form 2
        {0x05, "MODE2/2352", false},
        {0xAF, "MODE2/2352", false},
        {0x07, "MODE2/2352", false},
    };

    for (const Case &c : cases) {
        const std::string mds = TestDataDir() + "/mdsmodes.mds";
        MdsTrackSpec track;
        track.mode = c.mode;
        track.point = 1;
        track.sectorSize = 2352;
        track.startSector = 0;
        track.startOffset = 0;
        track.length = 4;
        WriteMdsFile(mds, {track}, "mdsmodes.mdf");

        CMDSFileDevice *disc = OpenMds(mds);
        CHECK(disc != nullptr);
        if (!disc) {
            continue;
        }

        char expected[128];
        snprintf(expected, sizeof(expected),
                 "FILE \"mdsmodes.mdf\" BINARY\n"
                 "  TRACK 01 %s\n"
                 "    INDEX 01 00:00:00\n",
                 c.cue);
        // Report the mode byte on failure; a bare string mismatch across
        // fourteen cases would not say which one broke.
        char msg[512];
        CHECK(disc->GetCueSheet() != nullptr);
        if (disc->GetCueSheet() && strcmp(disc->GetCueSheet(), expected) != 0) {
            snprintf(msg, sizeof(msg), "mode 0x%02x: expected TRACK 01 %s, got:\n%s",
                     c.mode, c.cue, disc->GetCueSheet());
            ReportFailure(__FILE__, __LINE__, msg);
        }
        if (disc->IsAudioTrack(0) != c.audio) {
            snprintf(msg, sizeof(msg), "mode 0x%02x: IsAudioTrack returned %d, expected %d",
                     c.mode, (int)disc->IsAudioTrack(0), (int)c.audio);
            ReportFailure(__FILE__, __LINE__, msg);
        }

        delete disc;
    }
}

// A real Alcohol 120% image does not store the pregap between two tracks, so
// its MDF is sparse: the disc has frames the file has no bytes for, and every
// track after the hole sits at a file offset well below lba * 2352. Alcohol
// writing a Video CD produced exactly this - 976 frames stored for a
// 1126-frame disc, track 2 at file offset 1237152 where the naive arithmetic
// says 1589952.
//
// Those frames are still part of the disc and READ CAPACITY counts them, so a
// host reading a little past the end of a track lands in the hole. That used
// to fail the whole transfer: FindTrackForLBA returned nothing, Seek returned
// (u64)-1, and the read errored. A real drive answers instead.
//
// Track modes here are 0xEC / 0xED because that is what Alcohol actually
// writes for Mode 2 Form 1 and Form 2 - not the 0xAC / 0xAD the published
// table lists.
TEST(mds_unstored_pregap_reads_as_zeros)
{
    std::vector<u8> raw = ReadAllBytes(kXa);
    if (raw.empty()) {
        return;
    }

    // Track 1 stops at LBA 24 rather than running the whole Form 1 run,
    // because LBA 25 onward in this image is empty ISO9660 system area. Ending
    // on a frame with real content is what makes "stored" and "hole"
    // distinguishable: both would otherwise read back as zeros.
    const u32 kTrack1Len = 25;
    const u32 kGap = 150;                       // frames Alcohol omits
    const u32 kTrack2LBA = kTrack1Len + kGap;
    const u32 kTotal = kTrack2LBA + kXaForm2Sectors;

    const std::string mds = TestDataDir() + "/mdsgap.mds";
    const std::string mdf = TestDataDir() + "/mdsgap.mdf";
    WriteBytes(mdf, raw);                        // only the stored frames

    MdsTrackSpec t1;
    t1.mode = 0xEC;
    t1.point = 1;
    t1.sectorSize = 2352;
    t1.startSector = 0;
    t1.startOffset = 0;
    t1.length = kTrack1Len;

    MdsTrackSpec t2;
    t2.mode = 0xED;
    t2.point = 2;
    t2.sectorSize = 2352;
    t2.startSector = kTrack2LBA;                 // 150 frames later on the disc
    t2.startOffset = (u64)kXaForm1Sectors * 2352; // but straight after in the file
    t2.pregap = kGap;
    t2.length = kXaForm2Sectors;

    WriteMdsFile(mds, {t1, t2}, "mdsgap.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // The hole counts toward capacity: the disc is 206 frames even though the
    // MDF holds 56.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    CHECK_EQ(lastLBA, kTotal - 1);

    auto read = [&bench](u32 lba, u32 blocks) {
        const u8 cdb[10] = {0x28, 0, (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                            0, (u8)(blocks >> 8), (u8)blocks, 0};
        return bench.SendCommand(cdb, sizeof(cdb), blocks * 2048);
    };
    auto allZero = [](const std::vector<u8> &d, size_t from, size_t len) {
        for (size_t i = from; i < from + len && i < d.size(); i++) {
            if (d[i] != 0) return false;
        }
        return true;
    };
    // A stored frame has to come back as the fixture's own bytes, not merely
    // as something non-zero: most of this image is legitimately zero-filled,
    // so "not zeros" would not tell a stored frame from a hole.
    auto matchesFixture = [&raw](const std::vector<u8> &d, size_t block, u32 lba) {
        const size_t at = block * 2048;
        if (d.size() < at + 2048) return false;
        return memcmp(d.data() + at, raw.data() + (size_t)lba * 2352 + 24, 2048) == 0;
    };

    // Real data before the hole, so the fixture is known good.
    auto pvd = read(16, 1);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    if (pvd.data.size() == 2048) {
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
    }

    // The regression: the first frame of the hole. This used to fail the
    // command outright.
    auto gap = read(kTrack1Len, 1);
    CHECK_EQ(gap.csw.bmCSWStatus, 0);
    CHECK_EQ(gap.data.size(), (size_t)2048);
    CHECK(allZero(gap.data, 0, 2048));

    // Track 2 still resolves through its own start_offset, so the hole did not
    // shift the file mapping.
    // Disc LBA kTrack2LBA lives at fixture frame kXaForm1Sectors, which is the
    // whole point: the hole moved the disc address without moving the file.
    auto mpeg = read(kTrack2LBA, 1);
    CHECK_EQ(mpeg.csw.bmCSWStatus, 0);
    CHECK(matchesFixture(mpeg.data, 0, kXaForm1Sectors));

    // Data running into the hole: two real frames then two empty ones.
    auto intoGap = read(kTrack1Len - 2, 4);
    CHECK_EQ(intoGap.csw.bmCSWStatus, 0);
    CHECK_EQ(intoGap.data.size(), (size_t)(4 * 2048));
    if (intoGap.data.size() == 4 * 2048) {
        CHECK(matchesFixture(intoGap.data, 0, kTrack1Len - 2)); // last stored frames
        CHECK(matchesFixture(intoGap.data, 1, kTrack1Len - 1));
        CHECK(allZero(intoGap.data, 2 * 2048, 2048));           // first of the hole
        CHECK(allZero(intoGap.data, 3 * 2048, 2048));
    }

    // The harder direction: out of the hole and back into real data. Nothing
    // advanced the file pointer across the empty frames, so this only works if
    // the reader re-seeks for the frames that do have bytes.
    auto outOfGap = read(kTrack2LBA - 2, 4);
    CHECK_EQ(outOfGap.csw.bmCSWStatus, 0);
    CHECK_EQ(outOfGap.data.size(), (size_t)(4 * 2048));
    if (outOfGap.data.size() == 4 * 2048) {
        CHECK(allZero(outOfGap.data, 0, 2048));
        CHECK(allZero(outOfGap.data, 1 * 2048, 2048));
        CHECK(matchesFixture(outOfGap.data, 2, kXaForm1Sectors));
        CHECK(matchesFixture(outOfGap.data, 3, kXaForm1Sectors + 1));
    }

    // Past the disc is still an error, not another hole.
    auto beyond = read(kTotal + 4, 1);
    CHECK(beyond.csw.bmCSWStatus != 0);

    delete disc;
}

// ---------------------------------------------------------------------------
// Subchannel images (2448-byte sectors)
// ---------------------------------------------------------------------------

// The reason MDS exists as a format: each sector carries its 96 bytes of P-W
// subchannel data appended to the 2352-byte frame, which is what copy
// protection schemes read. The reader has to strip those 96 bytes out of
// every user-data read while still being able to hand them back on request.
TEST(mds_subchannel_image_strips_and_exposes_subchannel_data)
{
    const u32 nSectors = 64;
    const std::string mds = TestDataDir() + "/mdssub.mds";
    const std::string mdf = TestDataDir() + "/mdssub.mdf";

    std::vector<u8> raw = RawMode1Sectors(kIso, 0, nSectors);
    if (raw.empty()) {
        CHECK(false);
        return;
    }

    // Interleave: [2352 bytes of frame][96 bytes of subchannel] per sector.
    std::vector<u8> image((size_t)nSectors * 2448);
    for (u32 lba = 0; lba < nSectors; lba++) {
        memcpy(image.data() + (size_t)lba * 2448, raw.data() + (size_t)lba * 2352, 2352);
        for (u32 i = 0; i < 96; i++) {
            image[(size_t)lba * 2448 + 2352 + i] = SubchannelByte(lba, i);
        }
    }
    WriteBytes(mdf, image);

    MdsTrackSpec track;
    track.mode = 0xAA;
    track.subchannel = 0x08; // P-W subchannels present
    track.point = 1;
    track.sectorSize = 2448;
    track.length = nSectors;
    WriteMdsFile(mds, {track}, "mdssub.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CHECK(disc->HasSubchannelData());

    // The subchannel bytes for a given LBA come back exactly as stored.
    for (u32 lba : {0u, 5u, 63u}) {
        u8 sub[96];
        memset(sub, 0, sizeof(sub));
        CHECK_EQ(disc->ReadSubchannel(lba, sub), 96);
        u8 expected[96];
        for (u32 i = 0; i < 96; i++) {
            expected[i] = SubchannelByte(lba, i);
        }
        CHECK_BYTES(sub, sizeof(sub), expected, sizeof(expected));
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Capacity must describe the 64 real frames, not the MDF byte count
    // divided by 2352 - the file is 2448 bytes per sector, so a naive
    // division reports 66 sectors on a 64-sector disc and lets a host read
    // two sectors off the end.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    CHECK_EQ(lastLBA, nSectors - 1);

    // User data must come back with the subchannel bytes stripped.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    if (pvd.data.size() == 2048) {
        CHECK_EQ(pvd.data[0], 0x01);
        CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
        CHECK(memcmp(pvd.data.data() + 40, "FREEDOS_TEST", 12) == 0);
    }

    const u32 lastSector = nSectors - 1;
    const u8 lastCdb[10] = {0x28, 0, 0, 0, 0, (u8)lastSector, 0, 0, 1, 0};
    auto last = bench.SendCommand(lastCdb, sizeof(lastCdb), 2048);
    CHECK_EQ(last.csw.bmCSWStatus, 0);
    CHECK_EQ(last.data.size(), (size_t)2048);
    if (last.data.size() == 2048) {
        u8 expected[2048];
        memcpy(expected, raw.data() + (size_t)lastSector * 2352 + 16, 2048);
        CHECK_BYTES(last.data.data(), last.data.size(), expected, sizeof(expected));
    }

    // Several sectors in one command: every frame in the batch has to have
    // its 96 subchannel bytes skipped, not just the first.
    //
    // Both batches below matter, and for different reasons. The reader has
    // to know which LBA it is at to find the track, and taking that from its
    // own file position does not work here: the physical position runs ahead
    // of lba * 2352 by one sector every 24.5, so it drifts. The batch at 40
    // catches a track lookup that fails outright; the batch at the tail
    // catches the drift, because by LBA 62 the derived LBA has run past the
    // end of a 64-sector disc, no track matches, and the read falls through
    // to a raw one that leaves the subchannel bytes in the user data of
    // every sector after the first.
    struct Batch { u32 start; u32 blocks; };
    for (Batch b : {Batch{40, 4}, Batch{nSectors - 2, 2}}) {
        const u8 batchCdb[10] = {0x28, 0, 0, 0, 0, (u8)b.start, 0, 0, (u8)b.blocks, 0};
        auto batch = bench.SendCommand(batchCdb, sizeof(batchCdb), b.blocks * 2048);
        CHECK_EQ(batch.csw.bmCSWStatus, 0);
        CHECK_EQ(batch.data.size(), (size_t)(b.blocks * 2048));
        if (batch.data.size() != b.blocks * 2048) {
            continue;
        }
        std::vector<u8> expected((size_t)b.blocks * 2048);
        for (u32 i = 0; i < b.blocks; i++) {
            memcpy(expected.data() + (size_t)i * 2048,
                   raw.data() + (size_t)(b.start + i) * 2352 + 16, 2048);
        }
        CHECK_BYTES(batch.data.data(), batch.data.size(), expected.data(), expected.size());
    }
}

// READ CD (0xBE) asking for the subchannel data alongside the user data.
// This is the whole point of the format - it is how a copy-protection check
// gets at the P-W subchannel - and it is the only path that carries those
// bytes to the host, so an MDS image is only as useful as this command.
//
// The reply interleaves one sector at a time: 2048 bytes of Mode 1 user data
// followed by that sector's 96 subchannel bytes.
TEST(mds_read_cd_returns_interleaved_subchannel_data)
{
    const u32 nSectors = 32;
    const std::string mds = TestDataDir() + "/mdsreadcd.mds";
    const std::string mdf = TestDataDir() + "/mdsreadcd.mdf";

    std::vector<u8> raw = RawMode1Sectors(kIso, 0, nSectors);
    if (raw.empty()) {
        CHECK(false);
        return;
    }
    std::vector<u8> image((size_t)nSectors * 2448);
    for (u32 lba = 0; lba < nSectors; lba++) {
        memcpy(image.data() + (size_t)lba * 2448, raw.data() + (size_t)lba * 2352, 2352);
        for (u32 i = 0; i < 96; i++) {
            image[(size_t)lba * 2448 + 2352 + i] = SubchannelByte(lba, i);
        }
    }
    WriteBytes(mdf, image);

    MdsTrackSpec track;
    track.mode = 0xAA;
    track.subchannel = 0x08;
    track.point = 1;
    track.sectorSize = 2448;
    track.length = nSectors;
    WriteMdsFile(mds, {track}, "mdsreadcd.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Expected sector type 2 (Mode 1), MCS user-data, and subchannel
    // selection 0x01 (raw P-W) in byte 10 - which is where MMC puts it.
    const u32 lba = 16;
    const u32 blocks = 2;
    const u8 cdb[12] = {0xBE, 0x02 << 2,
                        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                        0x00, 0x00, (u8)blocks,
                        0x10,  // MCS: user data
                        0x01,  // subchannel selection: raw P-W, 96 bytes
                        0x00};
    auto r = bench.SendCommand(cdb, sizeof(cdb), blocks * (2048 + 96));
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)(blocks * (2048 + 96)));
    if (r.data.size() != blocks * (2048 + 96)) {
        return;
    }

    std::vector<u8> expected((size_t)blocks * (2048 + 96));
    for (u32 i = 0; i < blocks; i++) {
        u8 *out = expected.data() + (size_t)i * (2048 + 96);
        memcpy(out, raw.data() + (size_t)(lba + i) * 2352 + 16, 2048);
        for (u32 j = 0; j < 96; j++) {
            out[2048 + j] = SubchannelByte(lba + i, j);
        }
    }
    CHECK_BYTES(r.data.data(), r.data.size(), expected.data(), expected.size());

    // Sanity: the user-data half really is the ISO's volume descriptor, so a
    // reply that happened to be the right length is not mistaken for a pass.
    CHECK(memcmp(r.data.data() + 1, "CD001", 5) == 0);
}

// ---------------------------------------------------------------------------
// Malformed and hostile .mds files
//
// These matter more than they look. The file browser lists every .mds on the
// card (scsitbservice.cpp), so anything a user renames to .mds reaches this
// parser, and a rejection has to be a clean "no" - the device stays up and
// serving. Nothing here should crash, hang, or read outside its own buffer.
// ---------------------------------------------------------------------------

TEST(mds_rejects_a_file_that_is_not_an_mds)
{
    const std::string mds = TestDataDir() + "/mdsbogus.mds";
    std::vector<u8> junk(4096);
    for (size_t i = 0; i < junk.size(); i++) {
        junk[i] = (u8)(i * 251 + 17); // no valid signature anywhere
    }
    WriteBytes(mds, junk);

    // The header's num_sessions field is junk too, which is the point: the
    // signature check has to be what decides, and everything after it has to
    // cope with the rest of the header being meaningless.
    CHECK(OpenMds(mds) == nullptr);
}

TEST(mds_rejects_a_truncated_header)
{
    const std::string mds = TestDataDir() + "/mdstrunc.mds";
    // A correct signature and nothing else: shorter than one header.
    std::vector<u8> bytes(16);
    memcpy(bytes.data(), "MEDIA DESCRIPTOR", 16);
    WriteBytes(mds, bytes);

    CHECK(OpenMds(mds) == nullptr);
}

TEST(mds_rejects_out_of_range_block_offsets)
{
    // Valid signature, but the session block table claims to live 8 MB into
    // a 4 KB file. Following that offset would read far outside the buffer.
    const std::string faroff = TestDataDir() + "/mdsfaroff.mds";
    std::vector<u8> bytes(4096, 0);
    MDS_Header hdr{};
    memcpy(hdr.signature, "MEDIA DESCRIPTOR", 16);
    hdr.num_sessions = 1;
    hdr.sessions_blocks_offset = 8 * 1024 * 1024;
    memcpy(bytes.data(), &hdr, sizeof(hdr));
    WriteBytes(faroff, bytes);
    CHECK(OpenMds(faroff) == nullptr);

    // The case that matters more: an offset that is past the end of THIS
    // file but small in absolute terms. Range checks against a fixed ceiling
    // rather than against the file's own length wave this through and read
    // several hundred KB of whatever follows the buffer on the heap.
    const std::string nearoff = TestDataDir() + "/mdsnearoff.mds";
    hdr.sessions_blocks_offset = 0x50000; // 320 KB into a 4 KB file
    memcpy(bytes.data(), &hdr, sizeof(hdr));
    WriteBytes(nearoff, bytes);
    CHECK(OpenMds(nearoff) == nullptr);

    // Same trick one level down: a plausible session table whose track block
    // offset points past the end.
    const std::string neartracks = TestDataDir() + "/mdsneartracks.mds";
    bytes.assign(4096, 0);
    hdr.sessions_blocks_offset = sizeof(MDS_Header);
    memcpy(bytes.data(), &hdr, sizeof(hdr));
    MDS_SessionBlock ses{};
    ses.session_number = 1;
    ses.num_all_blocks = 4;
    ses.num_nontrack_blocks = 3;
    ses.tracks_blocks_offset = 0x40000; // 256 KB into a 4 KB file
    memcpy(bytes.data() + sizeof(MDS_Header), &ses, sizeof(ses));
    WriteBytes(neartracks, bytes);
    CHECK(OpenMds(neartracks) == nullptr);

    // And a track whose extra block, and then a footer whose filename,
    // point off the end. Both are followed before the image is accepted.
    const std::string nearextra = TestDataDir() + "/mdsnearextra.mds";
    bytes.assign(4096, 0);
    ses.tracks_blocks_offset = sizeof(MDS_Header) + sizeof(MDS_SessionBlock);
    ses.num_all_blocks = 1;
    ses.num_nontrack_blocks = 0;
    memcpy(bytes.data(), &hdr, sizeof(hdr));
    memcpy(bytes.data() + sizeof(MDS_Header), &ses, sizeof(ses));
    MDS_TrackBlock blk{};
    blk.mode = 0xAA;
    blk.point = 1;
    blk.sector_size = 2352;
    blk.extra_offset = 0x30000; // 192 KB into a 4 KB file
    blk.footer_offset = 0x30000;
    memcpy(bytes.data() + ses.tracks_blocks_offset, &blk, sizeof(blk));
    WriteBytes(nearextra, bytes);
    CHECK(OpenMds(nearextra) == nullptr);
}

// A filename that runs to the end of the file without a terminator. Both the
// plain and the UTF-16 spelling have to be rejected rather than walked off
// the end of the buffer looking for a NUL that is not there.
TEST(mds_rejects_an_unterminated_mdf_filename)
{
    for (int wide = 0; wide < 2; wide++) {
        const std::string mds = TestDataDir() +
                                (wide ? "/mdsrawwide.mds" : "/mdsrawname.mds");

        // Build a well-formed single-track image, then overwrite everything
        // from the filename onward with non-zero bytes so no terminator
        // remains anywhere in the file.
        MdsTrackSpec track;
        track.length = 10;
        WriteMdsFile(mds, {track}, "x.mdf", wide != 0);

        std::vector<u8> bytes(FileSize(mds));
        FILE *f = fopen(mds.c_str(), "rb");
        CHECK(f != nullptr);
        if (!f) {
            continue;
        }
        size_t got = fread(bytes.data(), 1, bytes.size(), f);
        fclose(f);
        CHECK_EQ(got, bytes.size());

        // The name is the last thing in the file (see WriteMdsFile).
        const size_t nameStart = bytes.size() - (wide ? 12 : 6);
        for (size_t i = nameStart; i < bytes.size(); i++) {
            bytes[i] = 'A';
        }
        WriteBytes(mds, bytes);

        CHECK(OpenMds(mds) == nullptr);
    }
}

TEST(mds_rejects_a_missing_mdf)
{
    const std::string mds = TestDataDir() + "/mdsnomdf.mds";
    MdsTrackSpec track;
    track.length = 10;
    // Names a data file that does not exist.
    WriteMdsFile(mds, {track}, "mds-there-is-no-such-file.mdf");

    // Init() fails at f_open and the device is destroyed on the spot. This is
    // the one failure path that runs the destructor without a successful
    // Init(), so every member it touches has to be initialized by then.
    CHECK(OpenMds(mds) == nullptr);
}

TEST(mds_rejects_a_header_claiming_more_sessions_than_it_has)
{
    const std::string mds = TestDataDir() + "/mdsmanysessions.mds";

    std::vector<u8> bytes(4096, 0);
    MDS_Header hdr{};
    memcpy(hdr.signature, "MEDIA DESCRIPTOR", 16);
    hdr.num_sessions = 0xFFFF; // far beyond what any real disc has
    hdr.sessions_blocks_offset = sizeof(MDS_Header);
    memcpy(bytes.data(), &hdr, sizeof(hdr));
    WriteBytes(mds, bytes);

    CHECK(OpenMds(mds) == nullptr);
}

// A .mds with no usable track blocks: the session claims tracks but every
// entry is a lead-in, so there is no data file to open and nothing to read.
TEST(mds_rejects_a_session_with_no_real_tracks)
{
    const std::string mds = TestDataDir() + "/mdsnotracks.mds";
    WriteMdsFile(mds, {}, "mdsnotracks.mdf");

    CHECK(OpenMds(mds) == nullptr);
}

// The reader builds its CUE into a fixed buffer. A disc with the maximum
// number of track blocks the parser accepts must not run off the end of it.
TEST(mds_many_tracks_do_not_overrun_the_generated_cue)
{
    const u32 perTrack = 2; // sectors
    const u32 nTracks = 99; // the most a Red Book TOC can describe
    const std::string mds = TestDataDir() + "/mdsmany.mds";
    const std::string mdf = TestDataDir() + "/mdsmany.mdf";

    std::vector<u8> image((size_t)nTracks * perTrack * 2352);
    for (size_t i = 0; i < image.size(); i++) {
        image[i] = AudioByte(i);
    }
    WriteBytes(mdf, image);

    std::vector<MdsTrackSpec> tracks;
    for (u32 t = 0; t < nTracks; t++) {
        MdsTrackSpec spec;
        spec.mode = 0xA9; // audio, so the CUE lines stay short and uniform
        spec.point = (u8)(t + 1);
        spec.startSector = t * perTrack;
        spec.startOffset = (u64)t * perTrack * 2352;
        spec.pregap = 0;
        spec.length = perTrack;
        tracks.push_back(spec);
    }
    WriteMdsFile(mds, tracks, "mdsmany.mdf");

    CMDSFileDevice *disc = OpenMds(mds);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CHECK_EQ(disc->GetNumTracks(), (int)nTracks);

    // Every track must appear in the CUE, and the buffer must have held them
    // all: a truncated write would drop the tail, a miscounted one would run
    // past the end of the buffer.
    const char *cue = disc->GetCueSheet();
    CHECK(cue != nullptr);
    if (!cue) {
        return;
    }
    CHECK(strstr(cue, "  TRACK 01 AUDIO\n") != nullptr);
    CHECK(strstr(cue, "  TRACK 99 AUDIO\n") != nullptr);

    u32 seen = 0;
    for (const char *p = strstr(cue, "  TRACK "); p != nullptr; p = strstr(p + 1, "  TRACK ")) {
        seen++;
    }
    CHECK_EQ(seen, nTracks);

    // And the last track's audio still reads back byte for byte, so the
    // track table survived the same pressure the CUE builder was under.
    const u32 lastLBA = (nTracks - 1) * perTrack;
    const u64 off = disc->GetByteOffsetForLBA(lastLBA);
    CHECK_EQ(disc->Seek(off), off);
    u8 buf[2352];
    int n = disc->Read(buf, sizeof(buf));
    CHECK_EQ(n, (int)sizeof(buf));
    u8 expected[2352];
    for (u32 i = 0; i < 2352; i++) {
        expected[i] = AudioByte(off + i);
    }
    CHECK_BYTES(buf, (size_t)n, expected, sizeof(expected));
}
