//
// test_realimages.cpp
//
// Drives the gadget with the REAL disc-image reader (addon/discimage/
// cuebinfile.cpp) loading actual image files off disk, rather than the
// in-memory fake. This closes the gap the command-layer tests leave open:
// the cue parsing, per-track sector-size math, read-ahead cache, and file
// I/O are all real firmware code here, exercised end to end from a host
// command down to bytes read out of a file.
//
//   * A real ISO9660 disc: the tracked sdcard/image.iso.gz, decompressed by
//     the Makefile into USBODE_TESTDATA/image.iso.
//   * Synthetic CUE/BIN pairs written to disk at run time (a pure audio CD
//     and a mixed data+audio CD) whose byte content is known, so reads and
//     TOC/medium-type can be checked exactly. They are real cue sheets and
//     real BIN files parsed by the real reader; only the authoring is local.
//
#include "bench.h"
#include "fatfs_host.h"
#include "framework.h"

#include <discimage/cuebinfile.h>
#include <discimage/util.h>
#include <fatfs/ff.h>
#ifdef WITH_CHD
#include <discimage/chdfile.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

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

// Deterministic byte at a given file offset, so BIN content is reproducible
// and READ(10) payloads can be checked exactly.
static u8 PatternByte(u64 fileOffset)
{
    return (u8)(fileOffset * 31u + 7u);
}

static void WriteFileWithPattern(const std::string &path, u64 size)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return;
    }
    std::vector<u8> buf(64 * 1024);
    u64 written = 0;
    while (written < size) {
        u64 chunk = buf.size();
        if (chunk > size - written) {
            chunk = size - written;
        }
        for (u64 i = 0; i < chunk; i++) {
            buf[i] = PatternByte(written + i);
        }
        fwrite(buf.data(), 1, (size_t)chunk, f);
        written += chunk;
    }
    fclose(f);
}

static u64 FileSize(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return (u64)st.st_size;
}

static std::string FramesToMSF(u32 frames)
{
    u32 mm = frames / (60 * 75);
    u32 rem = frames % (60 * 75);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", mm, rem / 75, rem % 75);
    return buf;
}

// Construct a real CCueBinFileDevice from a BIN path plus its cue text (pass
// an empty cue to load the file as a plain ISO). Mirrors the ~10 lines of
// util.cpp's loadCueBinIsoFileDevice, minus the format dispatch.
static CCueBinFileDevice *OpenReader(const std::string &binPath, const std::string &cueText)
{
    FIL *fp = new FIL();
    if (f_open(fp, binPath.c_str(), FA_READ) != FR_OK) {
        delete fp;
        return nullptr;
    }
    char *cue = nullptr;
    if (!cueText.empty()) {
        cue = new char[cueText.size() + 1];
        memcpy(cue, cueText.c_str(), cueText.size() + 1);
    }
    CCueBinFileDevice *dev = new CCueBinFileDevice(fp, cue, MEDIA_TYPE::CD);
    delete[] cue; // the device copies it
    return dev;
}

// Load a reader from a real .cue file on disk: read the cue text back THROUGH
// the FatFs shim (f_open/f_read) rather than handing it an in-memory string,
// then open the .bin. This exercises the "cue sheet came off the filesystem"
// path the other tests skip.
static CCueBinFileDevice *OpenReaderFromCueFile(const std::string &cuePath,
                                                const std::string &binPath)
{
    FIL cf;
    if (f_open(&cf, cuePath.c_str(), FA_READ) != FR_OK) {
        return nullptr;
    }
    std::string cueText;
    char tmp[512];
    UINT br = 0;
    do {
        if (f_read(&cf, tmp, sizeof(tmp), &br) != FR_OK) {
            f_close(&cf);
            return nullptr;
        }
        cueText.append(tmp, br);
    } while (br == sizeof(tmp));
    f_close(&cf);
    return OpenReader(binPath, cueText);
}

// ---------------------------------------------------------------------------
// Real ISO9660 disc (tracked sdcard/image.iso.gz)
// ---------------------------------------------------------------------------

TEST(real_iso_reads_through_cuebin_reader)
{
    const std::string iso = TestDataDir() + "/image.iso";
    u64 size = FileSize(iso);
    CHECK(size > 0); // Makefile should have decompressed it
    if (size == 0) {
        return;
    }
    u32 totalBlocks = (u32)(size / 2048);

    CCueBinFileDevice *disc = OpenReader(iso, "");
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // READ CAPACITY: last addressable LBA is one below the block count.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    CHECK_EQ(cap.data.size(), (size_t)8);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    u32 blockSize = (cap.data[4] << 24) | (cap.data[5] << 16) | (cap.data[6] << 8) | cap.data[7];
    CHECK_EQ(blockSize, 2048u);
    CHECK_EQ(lastLBA, totalBlocks - 1);

    // READ(10) one sector, twice: the real file read path returns a full
    // sector with GOOD status, zero residue, and is stable across reads.
    const u8 rdCdb[10] = {0x28, 0, 0, 0, 0, 10, 0, 0, 1, 0}; // LBA 10, 1 block
    auto r1 = bench.SendCommand(rdCdb, sizeof(rdCdb), 2048);
    CHECK_EQ(r1.csw.bmCSWStatus, 0);
    CHECK_EQ(r1.csw.dCSWDataResidue, 0u);
    CHECK_EQ(r1.data.size(), (size_t)2048);
    auto r2 = bench.SendCommand(rdCdb, sizeof(rdCdb), 2048);
    CHECK_BYTES(r2.data.data(), r2.data.size(), r1.data.data(), r1.data.size());

    // Independent content oracle: LBA 16 of any ISO9660 disc is the Primary
    // Volume Descriptor, whose byte 0 is 0x01 and bytes 1..5 are the "CD001"
    // standard identifier. This pins the LBA->byte-offset math to real disc
    // content - a stable but wrong offset would pass the read-twice check
    // above but fail here.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0}; // LBA 16, 1 block
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK_EQ(pvd.data.size(), (size_t)2048);
    CHECK_EQ(pvd.data[0], 0x01); // primary volume descriptor
    CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);

    // A pure data disc reports medium type 0x01 and a data track in the TOC.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x01); // data CD

    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, 100, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 100);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);        // first track
    CHECK_EQ(toc.data[3], 0x01);        // last track
    CHECK_EQ(toc.data[5] & 0x04, 0x04); // track 1 control: data
}

// ---------------------------------------------------------------------------
// Synthetic pure-audio CD through the real reader
// ---------------------------------------------------------------------------

TEST(real_cuebin_audio_cd)
{
    const u32 nTracks = 3;
    const u32 sectorsPerTrack = 150; // 2 seconds
    const std::string bin = TestDataDir() + "/audio.bin";
    WriteFileWithPattern(bin, (u64)nTracks * sectorsPerTrack * 2352);

    std::string cue = "FILE \"audio.bin\" BINARY\n";
    for (u32 t = 0; t < nTracks; t++) {
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "  TRACK %02u AUDIO\n", t + 1);
        cue += hdr;
        cue += "    INDEX 01 " + FramesToMSF(t * sectorsPerTrack) + "\n";
    }

    CCueBinFileDevice *disc = OpenReader(bin, cue);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // All-audio disc -> medium type 0x02.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x02);

    // TOC: first/last track = 1/3.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);
    CHECK_EQ(toc.data[3], (u8)nTracks);
    CHECK_EQ(toc.data[5] & 0x04, 0x00); // track 1 control: audio (data bit clear)

    // PLAY AUDIO MSF starting at track 2 (LBA 150). CDB MSF is absolute, so
    // it carries LBA + 150 (the 2-second lead-in); the drive subtracts it
    // back off to recover the LBA.
    auto lbaToMsf = [](u32 lba, u8 &m, u8 &s, u8 &f) {
        u32 fr = lba + 150;
        m = (u8)(fr / (60 * 75));
        s = (u8)((fr / 75) % 60);
        f = (u8)(fr % 75);
    };
    u8 sm, ss, sf, em, es, ef;
    lbaToMsf(sectorsPerTrack, sm, ss, sf);          // start = LBA 150
    lbaToMsf(nTracks * sectorsPerTrack, em, es, ef); // end = leadout
    const u8 playCdb[10] = {0x47, 0x00, 0x00, sm, ss, sf, em, es, ef, 0x00};
    auto play = bench.SendCommand(playCdb, sizeof(playCdb), 0);
    CHECK_EQ(play.csw.bmCSWStatus, 0);
    CHECK_EQ(player.playCalls, 1);
    CHECK_EQ(player.lastPlayLBA, (u32)sectorsPerTrack); // LBA 150
}

// ---------------------------------------------------------------------------
// Synthetic mixed data+audio CD through the real reader
// ---------------------------------------------------------------------------

TEST(real_cuebin_mixed_mode)
{
    const u32 dataSectors = 100;  // MODE1/2048
    const u32 audioSectors = 150; // per audio track, 2352
    const u64 dataBytes = (u64)dataSectors * 2048;
    const std::string bin = TestDataDir() + "/mixed.bin";
    // BIN layout: data track at 2048/sector, then two audio tracks at 2352.
    WriteFileWithPattern(bin, dataBytes + (u64)2 * audioSectors * 2352);

    std::string cue = "FILE \"mixed.bin\" BINARY\n";
    cue += "  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n";
    cue += "  TRACK 02 AUDIO\n    INDEX 01 " + FramesToMSF(dataSectors) + "\n";
    cue += "  TRACK 03 AUDIO\n    INDEX 01 " + FramesToMSF(dataSectors + audioSectors) + "\n";

    CCueBinFileDevice *disc = OpenReader(bin, cue);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Data + audio -> medium type 0x03 (the #164-relevant path, now through
    // the real cue parser reading a real cue sheet).
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x03);

    // TOC: 3 tracks, track 1 data, track 2 audio.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);
    CHECK_EQ(toc.data[3], 0x03);
    CHECK_EQ(toc.data[5] & 0x04, 0x04); // track 1: data
    CHECK_EQ(toc.data[13] & 0x04, 0x00); // track 2: audio

    // READ(10) a sector out of the data track and confirm it is the exact
    // bytes we wrote (offset = LBA * 2048 for the data track).
    const u32 lba = 5;
    const u8 rdCdb[10] = {0x28, 0, 0, 0, 0, (u8)lba, 0, 0, 1, 0};
    auto rd = bench.SendCommand(rdCdb, sizeof(rdCdb), 2048);
    CHECK_EQ(rd.csw.bmCSWStatus, 0);
    CHECK_EQ(rd.data.size(), (size_t)2048);
    u8 expected[2048];
    for (u32 i = 0; i < 2048; i++) {
        expected[i] = PatternByte((u64)lba * 2048 + i);
    }
    CHECK_BYTES(rd.data.data(), rd.data.size(), expected, sizeof(expected));

    // Cross-track offset math: an audio-track sector lives past the
    // 2048->2352 sector-size change at the track-1/track-2 boundary, so its
    // byte offset is (data-track bytes) + relative_lba * 2352. Read 5 sectors
    // into audio track 2 straight from the reader and confirm both the
    // computed offset and the returned 2352-byte sector are exact. The
    // data-track READ(10) above never crosses the transition, so without this
    // a bug in the per-track offset accumulation could survive.
    const u32 audioLBA = dataSectors + 5; // 5 frames into audio track 2
    const u64 audioOff = disc->GetByteOffsetForLBA(audioLBA);
    CHECK_EQ(audioOff, dataBytes + (u64)5 * 2352);
    CHECK_EQ(disc->Seek(audioOff), audioOff);
    u8 aData[2352];
    int an = disc->Read(aData, sizeof(aData));
    CHECK_EQ(an, (int)sizeof(aData));
    u8 aExpected[2352];
    for (u32 i = 0; i < 2352; i++) {
        aExpected[i] = PatternByte(audioOff + i);
    }
    CHECK_BYTES(aData, (size_t)an, aExpected, sizeof(aExpected));
}

// ---------------------------------------------------------------------------
// Real FreeDOS ISO9660 + Joliet filesystem (testdata/freedos-test.iso.gz)
// ---------------------------------------------------------------------------

TEST(real_freedos_iso9660_filesystem)
{
    const std::string iso = TestDataDir() + "/freedos-test.iso";
    u64 size = FileSize(iso);
    CHECK(size > 0); // Makefile decompresses testdata/freedos-test.iso.gz
    if (size == 0) {
        return;
    }
    u32 totalBlocks = (u32)(size / 2048);

    CCueBinFileDevice *disc = OpenReader(iso, "");
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Capacity matches the real on-disk file.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    CHECK_EQ(lastLBA, totalBlocks - 1);

    // LBA 16 = Primary Volume Descriptor: type 0x01, "CD001", and the volume
    // identifier we authored (offset 40) - an oracle pinned to real content.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK_EQ(pvd.data.size(), (size_t)2048);
    CHECK_EQ(pvd.data[0], 0x01);
    CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
    CHECK(memcmp(pvd.data.data() + 40, "FREEDOS_TEST", 12) == 0);

    // LBA 17 = Joliet supplementary volume descriptor: type 0x02, "CD001".
    // Confirms the reader returns later sectors correctly, not just LBA 16.
    const u8 svdCdb[10] = {0x28, 0, 0, 0, 0, 17, 0, 0, 1, 0};
    auto svd = bench.SendCommand(svdCdb, sizeof(svdCdb), 2048);
    CHECK_EQ(svd.csw.bmCSWStatus, 0);
    CHECK_EQ(svd.data[0], 0x02);
    CHECK(memcmp(svd.data.data() + 1, "CD001", 5) == 0);

    // Data disc -> medium type 0x01, single data track in the TOC.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x01);
}

// ---------------------------------------------------------------------------
// Real shareware/freeware game disc (testdata/shareware.iso.gz): the Descent
// shareware episode and the SkyRoads freeware game. Reads the ENTIRE disc
// through the reader and checks it byte-for-byte against the raw file - an
// extent-agnostic oracle that stresses a real ~3.5 MB filesystem with
// multi-megabyte files spanning many sectors and read-ahead-cache refills.
// See testdata/README-testdata.md.
// ---------------------------------------------------------------------------

TEST(real_shareware_game_disc_full_readback)
{
    const std::string iso = TestDataDir() + "/shareware.iso";
    u64 size = FileSize(iso);
    CHECK(size > 0);
    if (size == 0) {
        return;
    }

    CCueBinFileDevice *disc = OpenReader(iso, "");
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // PVD sanity: "CD001" and the volume id we authored.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
    CHECK(memcmp(pvd.data.data() + 40, "SHAREWARE", 9) == 0);

    // Whole-disc readback: pull every byte through the real reader in an
    // odd-sized chunk (deliberately not aligned to the 128 KiB cache window,
    // so refills and cross-window reads are exercised) and compare against the
    // raw file read with plain stdio. Every sector of the real disc must match.
    FILE *raw = fopen(iso.c_str(), "rb");
    CHECK(raw != nullptr);
    if (!raw) {
        return;
    }
    disc->Seek(0);
    const size_t chunk = 7000;
    std::vector<u8> got(chunk), want(chunk);
    u64 pos = 0;
    bool mismatch = false;
    while (pos < size) {
        size_t n = (size_t)((size - pos < chunk) ? (size - pos) : chunk);
        int rn = disc->Read(got.data(), n);
        size_t wn = fread(want.data(), 1, n, raw);
        if (rn != (int)n || wn != n || memcmp(got.data(), want.data(), n) != 0) {
            mismatch = true;
            break;
        }
        pos += n;
    }
    fclose(raw);
    CHECK(!mismatch);
    CHECK_EQ(pos, size); // read the whole disc
}

// ---------------------------------------------------------------------------
// Real audio CD, cue sheet loaded off disk through the FatFs shim
// ---------------------------------------------------------------------------

TEST(real_audiocd_cue_loaded_via_fatfs)
{
    const std::string cue = TestDataDir() + "/audiocd.cue";
    const std::string bin = TestDataDir() + "/audiocd.bin";
    CHECK(FileSize(cue) > 0);
    CHECK(FileSize(bin) > 0);

    CCueBinFileDevice *disc = OpenReaderFromCueFile(cue, bin);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // All-audio disc parsed from the on-disk cue -> medium type 0x02.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x02);

    // TOC: three tracks.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);
    CHECK_EQ(toc.data[3], 0x03);

    // Exact bytes 3 sectors into track 2 (LBA 100). All-audio is contiguous
    // 2352-byte sectors, so the byte offset is LBA * 2352.
    const u32 lba = 100 + 3;
    const u64 off = disc->GetByteOffsetForLBA(lba);
    CHECK_EQ(off, (u64)lba * 2352);
    CHECK_EQ(disc->Seek(off), off);
    u8 buf[2352];
    int n = disc->Read(buf, sizeof(buf));
    CHECK_EQ(n, (int)sizeof(buf));
    u8 exp[2352];
    for (u32 i = 0; i < 2352; i++) {
        exp[i] = PatternByte(off + i);
    }
    CHECK_BYTES(buf, (size_t)n, exp, sizeof(exp));
}

// ---------------------------------------------------------------------------
// Real CD-DA disc built from the sound-test sample already in the repo
// ---------------------------------------------------------------------------

// sdcard/test.pcm.gz is the sample CCDPlayer::SoundTest() plays on hardware,
// and it is 16-bit stereo at 44.1 kHz, which is Red Book audio. The Makefile
// decompresses it and cuts it into a three-track disc, so this exercises the
// audio path with the same real signal data the device itself ships instead
// of a generated pattern, and without committing another image.
//
// The oracle is the decompressed PCM itself: every byte the drive returns is
// compared against the same offset in the source file. That is independent of
// how the reader computes the offset, so a wrong-but-consistent seek cannot
// satisfy it.
TEST(real_audio_from_repo_pcm_sample)
{
    const std::string cue = TestDataDir() + "/realaudio.cue";
    const std::string bin = TestDataDir() + "/realaudio.bin";
    CHECK(FileSize(cue) > 0);

    const u64 binSize = FileSize(bin);
    CHECK_EQ(binSize, (u64)438 * 2352); // whole sectors only

    CCueBinFileDevice *disc = OpenReaderFromCueFile(cue, bin);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // All-audio disc -> medium type 0x02, the code Win9x MCICDA needs before
    // it will treat the disc as playable.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x02);

    // Three tracks, and the leadout is where the audio actually ends.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01); // first track
    CHECK_EQ(toc.data[3], 0x03); // last track
    const u8 expectedToc[36] = {
        0x00, 0x22, 0x01, 0x03,
        0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // track 1, LBA 0
        0x00, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x96, // track 2, LBA 150
        0x00, 0x10, 0x03, 0x00, 0x00, 0x00, 0x01, 0x2C, // track 3, LBA 300
        0x00, 0x10, 0xAA, 0x00, 0x00, 0x00, 0x01, 0xB6, // leadout, LBA 438
    };
    CHECK_BYTES(toc.data.data(), toc.data.size(), expectedToc, sizeof(expectedToc));

    // Read the source PCM once and use it as the oracle.
    std::vector<u8> pcm(binSize);
    FILE *f = fopen(bin.c_str(), "rb");
    CHECK(f != nullptr);
    if (!f) {
        return;
    }
    size_t got = fread(pcm.data(), 1, pcm.size(), f);
    fclose(f);
    CHECK_EQ(got, pcm.size());

    // Sectors at the start, across the track 1/2 boundary, inside track 3,
    // and the very last addressable sector on the disc.
    const u32 lbas[] = {0, 149, 150, 305, 437};
    for (u32 lba : lbas) {
        const u64 off = disc->GetByteOffsetForLBA(lba);
        CHECK_EQ(off, (u64)lba * 2352); // contiguous audio
        CHECK_EQ(disc->Seek(off), off);
        u8 buf[2352];
        int n = disc->Read(buf, sizeof(buf));
        CHECK_EQ(n, (int)sizeof(buf));
        CHECK_BYTES(buf, (size_t)n, pcm.data() + off, 2352);
    }

    // And the same bytes on the wire through READ CD (0xBE) with expected
    // sector type 1, CD-DA. That is the command a ripper or a player uses to
    // pull raw audio, and unlike READ(10) it returns the full 2352-byte
    // sector rather than 2048 bytes of cooked user data. Two sectors at once
    // so the per-block stride is covered as well as the start offset.
    const u32 lba = 200;
    const u8 readCdb[12] = {0xBE, 0x01 << 2,
                            (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                            0x00, 0x00, 0x02, // two blocks
                            0x10,             // MCS: user data
                            0x00, 0x00};
    auto r = bench.SendCommand(readCdb, sizeof(readCdb), 2 * 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)(2 * 2352));
    CHECK_BYTES(r.data.data(), r.data.size(), pcm.data() + (u64)lba * 2352, 2 * 2352);
}

// ---------------------------------------------------------------------------
// Real mixed-mode CD, cue sheet loaded off disk through the FatFs shim
// ---------------------------------------------------------------------------

TEST(real_mixed_cue_loaded_via_fatfs)
{
    const std::string cue = TestDataDir() + "/mixed.cue";
    const std::string bin = TestDataDir() + "/mixed.bin";
    CHECK(FileSize(cue) > 0);
    CHECK(FileSize(bin) > 0);

    CCueBinFileDevice *disc = OpenReaderFromCueFile(cue, bin);
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Data + audio -> medium type 0x03.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x03);

    // Data track (2048/sector): READ(10) at LBA 5 returns exact bytes.
    const u32 dataSectors = 100;
    const u64 dataBytes = (u64)dataSectors * 2048;
    const u8 rdCdb[10] = {0x28, 0, 0, 0, 0, 5, 0, 0, 1, 0};
    auto rd = bench.SendCommand(rdCdb, sizeof(rdCdb), 2048);
    CHECK_EQ(rd.csw.bmCSWStatus, 0);
    u8 dexp[2048];
    for (u32 i = 0; i < 2048; i++) {
        dexp[i] = PatternByte((u64)5 * 2048 + i);
    }
    CHECK_BYTES(rd.data.data(), rd.data.size(), dexp, sizeof(dexp));

    // Audio track past the 2048->2352 boundary: 5 sectors into track 2
    // (LBA 100). Byte offset must switch sector size at the boundary.
    const u32 audioLBA = dataSectors + 5;
    const u64 off = disc->GetByteOffsetForLBA(audioLBA);
    CHECK_EQ(off, dataBytes + (u64)5 * 2352);
    CHECK_EQ(disc->Seek(off), off);
    u8 abuf[2352];
    int an = disc->Read(abuf, sizeof(abuf));
    CHECK_EQ(an, (int)sizeof(abuf));
    u8 aexp[2352];
    for (u32 i = 0; i < 2352; i++) {
        aexp[i] = PatternByte(off + i);
    }
    CHECK_BYTES(abuf, (size_t)an, aexp, sizeof(aexp));
}

// Mount through the same factory the firmware uses.
static IImageDevice *LoadThroughProductionLoader(const std::string &cuePath)
{
    return loadCueBinIsoFileDevice(cuePath.c_str());
}

// Split and combined versions must return identical bytes at file boundaries.
TEST(real_split_rip_reads_identically_to_the_single_file_disc)
{
    const std::string dir = TestDataDir();
    CHECK_EQ(FileSize(dir + "/splitaudio-t1.bin"), (u64)150 * 2352);
    CHECK_EQ(FileSize(dir + "/splitaudio-t2.bin"), (u64)150 * 2352);
    CHECK_EQ(FileSize(dir + "/splitaudio-t3.bin"), (u64)138 * 2352);

    IImageDevice *split = LoadThroughProductionLoader(dir + "/splitaudio.cue");
    CCueBinFileDevice *whole = OpenReaderFromCueFile(dir + "/realaudio.cue",
                                                     dir + "/realaudio.bin");
    CHECK(split != nullptr);
    CHECK(whole != nullptr);
    if (!split || !whole) {
        delete split;
        delete whole;
        return;
    }

    CHECK_EQ(split->GetSize(), whole->GetSize());
    CHECK_EQ(split->GetSize(), (u64)438 * 2352);

    // Track boundaries and either side of them, plus the last sector.
    const u32 lbas[] = {0, 1, 149, 150, 151, 299, 300, 301, 437};
    for (u32 lba : lbas) {
        u64 sOff = split->GetByteOffsetForLBA(lba);
        u64 wOff = whole->GetByteOffsetForLBA(lba);
        CHECK_EQ(sOff, wOff);

        u8 sbuf[2352];
        u8 wbuf[2352];
        CHECK_EQ(split->Seek(sOff), sOff);
        CHECK_EQ(whole->Seek(wOff), wOff);
        CHECK_EQ(split->Read(sbuf, sizeof(sbuf)), (int)sizeof(sbuf));
        CHECK_EQ(whole->Read(wbuf, sizeof(wbuf)), (int)sizeof(wbuf));
        CHECK_BYTES(sbuf, sizeof(sbuf), wbuf, sizeof(wbuf));
    }

    delete split;
    delete whole;
}

// File sizes must place split tracks at their real LBAs.
TEST(real_split_rip_toc_reports_the_real_track_starts)
{
    IImageDevice *split = LoadThroughProductionLoader(TestDataDir() + "/splitaudio.cue");
    CHECK(split != nullptr);
    if (!split) {
        return;
    }

    CGadgetTestBench bench(split);
    bench.Activate();
    bench.RequestSense();

    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    const u8 expectedToc[36] = {
        0x00, 0x22, 0x01, 0x03,
        0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // track 1, LBA 0
        0x00, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x96, // track 2, LBA 150
        0x00, 0x10, 0x03, 0x00, 0x00, 0x00, 0x01, 0x2C, // track 3, LBA 300
        0x00, 0x10, 0xAA, 0x00, 0x00, 0x00, 0x01, 0xB6, // leadout, LBA 438
    };
    CHECK_BYTES(toc.data.data(), toc.data.size(), expectedToc, sizeof(expectedToc));
}

// next_track() invalidates the previous track pointer, which zeroed lengths.
TEST(real_split_rip_read_track_information_reports_track_length)
{
    IImageDevice *split = LoadThroughProductionLoader(TestDataDir() + "/splitaudio.cue");
    CHECK(split != nullptr);
    if (!split) {
        return;
    }

    CGadgetTestBench bench(split);
    bench.Activate();
    bench.RequestSense();

    // Address type 1 (track number), tracks 1 and 3: 150 and 138 sectors.
    const u32 expectedLength[3] = {150, 150, 138};
    for (u8 track = 1; track <= 3; track++) {
        const u8 rtiCdb[10] = {0x52, 0x01, 0, 0, 0, track, 0, 0, 40, 0};
        auto rti = bench.SendCommand(rtiCdb, sizeof(rtiCdb), 40);
        CHECK_EQ(rti.csw.bmCSWStatus, 0);
        CHECK(rti.data.size() >= 28);
        if (rti.data.size() < 28) {
            continue;
        }
        u32 length = (rti.data[24] << 24) | (rti.data[25] << 16) |
                     (rti.data[26] << 8) | rti.data[27];
        CHECK_EQ(length, expectedLength[track - 1]);
    }
}

// A read crossing a .bin boundary must come back whole; short is a medium error.
TEST(real_split_rip_read_crosses_a_bin_boundary)
{
    const std::string dir = TestDataDir();
    IImageDevice *split = LoadThroughProductionLoader(dir + "/splitaudio.cue");
    CCueBinFileDevice *whole = OpenReaderFromCueFile(dir + "/realaudio.cue",
                                                     dir + "/realaudio.bin");
    CHECK(split != nullptr);
    CHECK(whole != nullptr);
    if (!split || !whole) {
        delete split;
        delete whole;
        return;
    }

    // Four sectors starting two before the track 1/2 boundary at LBA 150.
    const u64 off = (u64)148 * 2352;
    const size_t want = 4 * 2352;
    std::vector<u8> sbuf(want), wbuf(want);
    CHECK_EQ(split->Seek(off), off);
    CHECK_EQ(whole->Seek(off), off);
    CHECK_EQ(split->Read(sbuf.data(), want), (int)want);
    CHECK_EQ(whole->Read(wbuf.data(), want), (int)want);
    CHECK_BYTES(sbuf.data(), sbuf.size(), wbuf.data(), wbuf.size());

    const size_t all = (size_t)438 * 2352;
    std::vector<u8> sall(all), wall(all);
    CHECK_EQ(split->Seek(0), (u64)0);
    CHECK_EQ(whole->Seek(0), (u64)0);
    CHECK_EQ(split->Read(sall.data(), all), (int)all);
    CHECK_EQ(whole->Read(wall.data(), all), (int)all);
    CHECK_BYTES(sall.data(), sall.size(), wall.data(), wall.size());

    delete split;
    delete whole;
}

// Reading at exactly the end is how a caller finds the end: 0 bytes, no error.
TEST(real_split_rip_read_at_eof_returns_zero)
{
    IImageDevice *split = LoadThroughProductionLoader(TestDataDir() + "/splitaudio.cue");
    CHECK(split != nullptr);
    if (!split) {
        return;
    }

    const u64 size = split->GetSize();
    CHECK_EQ(size, (u64)438 * 2352);
    CHECK_EQ(split->Seek(size), size);
    u8 buf[2352];
    CHECK_EQ(split->Read(buf, sizeof(buf)), 0);

    delete split;
}

// Mixed-mode fixture: MODE1/2352 data followed by split audio.
TEST(real_split_rip_mixed_mode_toc_and_data_track)
{
    IImageDevice *disc = LoadThroughProductionLoader(TestDataDir() + "/splitmixed.cue");
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    const u8 expectedToc[36] = {
        0x00, 0x22, 0x01, 0x03,
        0x00, 0x14, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // track 1 data, LBA 0
        0x00, 0x10, 0x02, 0x00, 0x00, 0x00, 0x01, 0x2C, // track 2 audio, LBA 300
        0x00, 0x10, 0x03, 0x00, 0x00, 0x00, 0x01, 0xC2, // track 3 audio, LBA 450
        0x00, 0x10, 0xAA, 0x00, 0x00, 0x00, 0x02, 0x4C, // leadout, LBA 588
    };
    CHECK_BYTES(toc.data.data(), toc.data.size(), expectedToc, sizeof(expectedToc));

    // Data + audio is medium type 0x03; 0x02 stops a host looking for a filesystem.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x03);

    // Track 1 is real ISO9660, so LBA 16 is the Primary Volume Descriptor.
    const u8 pvdCdb[10] = {0x28, 0, 0, 0, 0, 16, 0, 0, 1, 0};
    auto pvd = bench.SendCommand(pvdCdb, sizeof(pvdCdb), 2048);
    CHECK_EQ(pvd.csw.bmCSWStatus, 0);
    CHECK_EQ(pvd.data.size(), (size_t)2048);
    CHECK_EQ(pvd.data[0], 0x01);
    CHECK(memcmp(pvd.data.data() + 1, "CD001", 5) == 0);
}

// Empty files must be rejected before they collapse later file offsets.
TEST(real_split_rip_empty_bin_is_refused)
{
    IImageDevice *dev = LoadThroughProductionLoader(TestDataDir() + "/splitempty.cue");
    CHECK(dev == nullptr);
    delete dev;

    // Naming the file proves this is not a stale message from another load.
    const char *err = GetLastImageLoadError();
    CHECK(err != nullptr && strstr(err, "splitempty-t2.bin") != nullptr);
}

// One missing track file must refuse the mount rather than serve it short.
TEST(real_split_rip_missing_bin_is_refused)
{
    IImageDevice *dev = LoadThroughProductionLoader(TestDataDir() + "/splitmissing.cue");
    CHECK(dev == nullptr);
    delete dev;

    const char *err = GetLastImageLoadError();
    CHECK(err != nullptr && strstr(err, "splitaudio-gone.bin") != nullptr);
}

// Every .bin needs its own link map, or its seeks walk the FAT chain.
TEST(real_split_rip_enables_fast_seek_for_every_bin)
{
    FatFsHostResetLinkmapCount();
    IImageDevice *split = LoadThroughProductionLoader(TestDataDir() + "/splitaudio.cue");
    CHECK(split != nullptr);
    CHECK_EQ(FatFsHostLinkmapCount(), 3u);
    if (split) {
        CHECK_EQ(split->GetDataFileCount(), 3);
    }
    delete split;

    FatFsHostResetLinkmapCount();
    IImageDevice *whole = LoadThroughProductionLoader(TestDataDir() + "/realaudio.cue");
    CHECK(whole != nullptr);
    CHECK_EQ(FatFsHostLinkmapCount(), 1u);
    delete whole;
}

// A FILE name that escapes the cue's directory must be refused before it is
// opened, even when the file it points at exists.
TEST(real_split_rip_rejects_paths_outside_the_cue_directory)
{
    const std::string dir = TestDataDir();
    CHECK(FileSize(dir + "/../outside.bin") > 0);

    const char *cues[] = {"/splittraversal.cue", "/splitbackslash.cue", "/splitabsolute.cue"};
    const char *names[] = {"../outside.bin", "..\\outside.bin", "/absolute.bin"};
    for (int i = 0; i < 3; i++) {
        IImageDevice *dev = LoadThroughProductionLoader(dir + cues[i]);
        CHECK(dev == nullptr);
        delete dev;
        const char *err = GetLastImageLoadError();
        CHECK(err != nullptr && strstr(err, names[i]) != nullptr);
    }
}

// "." components and nested relative paths stay inside the directory.
TEST(real_split_rip_accepts_safe_relative_paths)
{
    const std::string dir = TestDataDir();
    const char *cues[] = {"/splitdot.cue", "/splitnested.cue"};
    for (int i = 0; i < 2; i++) {
        IImageDevice *dev = LoadThroughProductionLoader(dir + cues[i]);
        CHECK(dev != nullptr);
        if (!dev) {
            continue;
        }
        CHECK_EQ(dev->GetDataFileCount(), 2);
        CHECK_EQ(dev->GetSize(), (u64)300 * 2352);

        u8 buf[2352];
        CHECK_EQ(dev->Seek(dev->GetByteOffsetForLBA(150)), (u64)150 * 2352);
        CHECK_EQ(dev->Read(buf, sizeof(buf)), (int)sizeof(buf));
        delete dev;
    }
}

// A device holding fewer files than its cue names cannot place an LBA, and a
// single-file offset would address the wrong .bin.
TEST(split_resolution_failure_returns_the_invalid_offset)
{
    const std::string dir = TestDataDir();
    const std::string cue =
        "FILE \"splitaudio-t1.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
        "FILE \"splitaudio-t2.bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
        "FILE \"splitaudio-t3.bin\" BINARY\n  TRACK 03 AUDIO\n    INDEX 01 00:00:00\n";

    CCueBinFileDevice *dev = OpenReader(dir + "/splitaudio-t1.bin", cue);
    CHECK(dev != nullptr);
    if (!dev) {
        return;
    }
    FIL *second = new FIL();
    CHECK(f_open(second, (dir + "/splitaudio-t2.bin").c_str(), FA_READ) == FR_OK);
    CHECK(dev->AddDataFile(second));
    CHECK_EQ(dev->GetDataFileCount(), 2);

    // The single-file helper would answer 0 and 150 * 2352 here.
    CHECK_EQ(dev->GetByteOffsetForLBA(0), (u64)-1);
    CHECK_EQ(dev->GetByteOffsetForLBA(150), (u64)-1);
    CHECK_EQ(dev->Seek(dev->GetByteOffsetForLBA(0)), (u64)-1);
    delete dev;
}

// A stale FILE line must still take the data file from the cue's own name.
TEST(real_single_file_cue_with_stale_file_name_still_loads)
{
    IImageDevice *dev = LoadThroughProductionLoader(TestDataDir() + "/stalename.cue");
    CHECK(dev != nullptr);
    if (!dev) {
        return;
    }

    CHECK_EQ(dev->GetSize(), (u64)438 * 2352);
    CHECK_EQ(dev->GetDataFileCount(), 1);

    u8 buf[2352];
    CHECK_EQ(dev->Seek(0), (u64)0);
    CHECK_EQ(dev->Read(buf, sizeof(buf)), (int)sizeof(buf));

    delete dev;
}

// ---------------------------------------------------------------------------
// Real CHD image (tracked sdcard/usbode-audio-test.chd) through libchdr
// ---------------------------------------------------------------------------

#ifdef WITH_CHD
static std::string SdcardDir()
{
#ifdef USBODE_SDCARD
    return USBODE_SDCARD;
#else
    return "../sdcard";
#endif
}

TEST(real_chd_loads_through_libchdr)
{
    const std::string chd = SdcardDir() + "/usbode-audio-test.chd";
    CHECK(FileSize(chd) > 0);
    if (FileSize(chd) == 0) {
        return;
    }

    CCHDFileDevice *disc = new CCHDFileDevice(chd.c_str(), MEDIA_TYPE::CD);
    bool ok = disc->Init();
    CHECK(ok); // real libchdr open + metadata parse
    if (!ok) {
        return;
    }

    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player);
    bench.Activate();
    bench.RequestSense();

    // TOC parses from the CHD metadata: a sensible track range.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01); // first track number
    u8 lastTrack = toc.data[3];
    CHECK(lastTrack >= 1);

    // Medium type is one of the valid CD codes.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK(ms.data[2] == 0x01 || ms.data[2] == 0x02 || ms.data[2] == 0x03);

    // READ CAPACITY reports a non-empty disc.
    const u8 capCdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto cap = bench.SendCommand(capCdb, sizeof(capCdb), 8);
    CHECK_EQ(cap.csw.bmCSWStatus, 0);
    u32 lastLBA = (cap.data[0] << 24) | (cap.data[1] << 16) | (cap.data[2] << 8) | cap.data[3];
    CHECK(lastLBA > 0);

    // Force a real hunk decompression. The checks above only parse CHD
    // metadata; this reads sector 0 straight through CCHDFileDevice::Read,
    // which calls chd_read and runs the real zlib/zstd/lzma decoder. A broken
    // decode path returns a short/negative read here instead of silently
    // passing a metadata-only test. Read it twice to confirm the decoded
    // bytes are stable (cold decode vs cached hunk agree).
    disc->Seek(0);
    std::vector<u8> sec0(2048);
    int n0 = disc->Read(sec0.data(), sec0.size());
    CHECK_EQ(n0, (int)sec0.size());
    disc->Seek(0);
    std::vector<u8> sec0b(2048);
    int n0b = disc->Read(sec0b.data(), sec0b.size());
    CHECK_EQ(n0b, (int)sec0b.size());
    CHECK_BYTES(sec0b.data(), sec0b.size(), sec0.data(), sec0.size());
}

// A mixed-mode CHD we build ourselves with chdman (testdata/mixed.chd:
// one MODE1/2048 data track + two audio tracks). Exercises libchdr on a
// real, tool-produced multi-track CHD rather than only the tracked audio one.
TEST(real_mixed_chd_through_libchdr)
{
    const std::string chd = TestDataDir() + "/mixed.chd";
    CHECK(FileSize(chd) > 0);
    if (FileSize(chd) == 0) {
        return;
    }

    CCHDFileDevice *disc = new CCHDFileDevice(chd.c_str(), MEDIA_TYPE::CD);
    bool ok = disc->Init();
    CHECK(ok);
    if (!ok) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // TOC: three tracks (data + 2 audio), track 1 is data.
    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01);        // first track
    CHECK_EQ(toc.data[3], 0x03);        // last track
    CHECK_EQ(toc.data[5] & 0x04, 0x04); // track 1: data

    // Data + audio -> medium type 0x03.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x03);

    // Decode a data-track hunk and check the bytes are EXACT, not just stable.
    // chdman stored the MODE1/2048 track's user data (our PatternByte fill);
    // reading sector 0 back runs chd_read -> the real LZMA/deflate decoder and
    // must reproduce those exact bytes. A broken decode (or a zeroed buffer)
    // fails here rather than passing a metadata-only or read-twice check.
    disc->Seek(0);
    std::vector<u8> sec(2048);
    CHECK_EQ(disc->Read(sec.data(), sec.size()), (int)sec.size());
    u8 exp[2048];
    for (u32 i = 0; i < 2048; i++) {
        exp[i] = PatternByte(i); // data-track sector 0, file offset 0
    }
    CHECK_BYTES(sec.data(), sec.size(), exp, sizeof(exp));

    // Same through the full host path: READ(10) a data sector further in and
    // confirm exact bytes (exercises the gadget's LBA->offset->chd_read chain).
    const u8 rdCdb[10] = {0x28, 0, 0, 0, 0, 5, 0, 0, 1, 0}; // LBA 5, 1 block
    auto rd = bench.SendCommand(rdCdb, sizeof(rdCdb), 2048);
    CHECK_EQ(rd.csw.bmCSWStatus, 0);
    u8 exp5[2048];
    for (u32 i = 0; i < 2048; i++) {
        exp5[i] = PatternByte((u64)5 * 2048 + i);
    }
    CHECK_BYTES(rd.data.data(), rd.data.size(), exp5, sizeof(exp5));

    // Audio track 2 (starts at LBA 100), read straight from the reader. This
    // exercises CCHDFileDevice's audio path: track-type detection plus the
    // pair byte-swap. chdman stores CD audio big-endian, so the swap converts
    // it back to the host's little-endian - round-tripping to the original
    // mixed.bin bytes (PatternByte from the audio track's file offset). A
    // broken or missing swap would NOT reproduce these exact bytes.
    const u64 audioFileOff = (u64)100 * 2048; // audio track 2 starts at LBA 100
    disc->Seek((u64)100 * 2352);                       // LBA 100 * 2352/sector
    u8 aud[2352];
    CHECK_EQ(disc->Read(aud, sizeof(aud)), (int)sizeof(aud));
    u8 aexp[2352];
    for (u32 i = 0; i < 2352; i++) {
        aexp[i] = PatternByte(audioFileOff + i);
    }
    CHECK_BYTES(aud, sizeof(aud), aexp, sizeof(aexp));
}

// A CHD compressed with cdfl (FLAC) only.
//
// The other CHD fixtures list cdlz/cdzl/cdfl in their headers, but chdman
// picks whichever codec wins per hunk, and for synthetic pattern data that is
// essentially always LZMA or deflate -- so libchdr's FLAC decoder
// (libchdr_flac.c, plus the CD-specific de-interleave in libchdr_cdrom.c) was
// compiled but never actually executed by the suite. Real audio rips are the
// opposite: FLAC is what wins on genuine 16-bit stereo audio, so this is the
// path a user's music CD image most likely takes.
//
// Built with `chdman createcd -c cdfl` from the same audiocd.bin the CUE/BIN
// tests use, which lets the decoded audio be checked against the original
// bytes rather than merely against itself.
TEST(real_flac_chd_audio_decodes_byte_exact)
{
    const std::string chd = TestDataDir() + "/audiocd-flac.chd";
    CHECK(FileSize(chd) > 0);
    if (FileSize(chd) == 0) {
        return;
    }

    CCHDFileDevice *disc = new CCHDFileDevice(chd.c_str(), MEDIA_TYPE::CD);
    bool ok = disc->Init();
    CHECK(ok);
    if (!ok) {
        return;
    }

    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // All-audio disc -> medium type 0x02, three tracks.
    const u8 msCdb[10] = {0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 128, 0};
    auto ms = bench.SendCommand(msCdb, sizeof(msCdb), 128);
    CHECK_EQ(ms.csw.bmCSWStatus, 0);
    CHECK_EQ(ms.data[2], 0x02);

    const u8 tocCdb[10] = {0x43, 0x00, 0, 0, 0, 0, 0, 0, (u8)200, 0};
    auto toc = bench.SendCommand(tocCdb, sizeof(tocCdb), 200);
    CHECK_EQ(toc.csw.bmCSWStatus, 0);
    CHECK_EQ(toc.data[2], 0x01); // first track
    CHECK_EQ(toc.data[3], 0x03); // last track

    // Sector 0 of track 1, FLAC-decoded and byte-swapped back to little
    // endian, must equal the original audiocd.bin bytes. FLAC is lossless, so
    // anything less than an exact match means the decode, the de-interleave,
    // or the pair swap is wrong.
    disc->Seek(0);
    u8 first[2352];
    CHECK_EQ(disc->Read(first, sizeof(first)), (int)sizeof(first));
    u8 fexp[2352];
    for (u32 i = 0; i < 2352; i++) {
        fexp[i] = PatternByte(i);
    }
    CHECK_BYTES(first, sizeof(first), fexp, sizeof(fexp));

    // A sector well inside track 2, so the read lands in a later hunk and the
    // decoder has to seek rather than replay the first block it decoded.
    const u32 lba = 100 + 17;
    const u64 off = (u64)lba * 2352;
    CHECK_EQ(disc->GetByteOffsetForLBA(lba), off);
    CHECK_EQ(disc->Seek(off), off);
    u8 mid[2352];
    CHECK_EQ(disc->Read(mid, sizeof(mid)), (int)sizeof(mid));
    u8 mexp[2352];
    for (u32 i = 0; i < 2352; i++) {
        mexp[i] = PatternByte(off + i);
    }
    CHECK_BYTES(mid, sizeof(mid), mexp, sizeof(mexp));
}
#endif // WITH_CHD
