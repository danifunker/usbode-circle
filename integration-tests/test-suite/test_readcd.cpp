//
// test_readcd.cpp
//
// READ CD (0xBE) field selection.
//
// READ CD is the only command that can serve a Mode 2 Form 2 sector's real
// 2324-byte payload, because READ(10) has no way to express a sector that is
// not 2048 bytes. Which parts of the sector come back is chosen by the field
// selection in CDB byte 9; the expected sector type in byte 1 says what kind of
// sector the host will accept and how big each field is, but it does not choose
// the fields.
//
// The command used to ignore byte 9 whenever byte 1 named a type, and to
// mis-map byte 9's bottom three bits when it did not. Both were found from a
// device trace of a real host reading a real disc, and both are pinned here.
//
#include "bench.h"
#include "framework.h"

#include <discimage/cuebinfile.h>
#include <fatfs/ff.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// A real Video CD extract: 32 Mode 2 Form 1 sectors followed by 24 Mode 2
// Form 2 sectors, raw 2352 bytes each. Shared with the MDS tests.
static const u32 kForm1Sectors = 32;
static const u32 kForm2Sectors = 24;

// Each test file keeps its own copy: these are separate translation units and
// the helper is a one-liner over a compile-time define.
static std::string ReadCdTestDataDir()
{
#ifdef USBODE_TESTDATA
    return USBODE_TESTDATA;
#else
    return "out/images";
#endif
}

static std::string XaBinPath(void)
{
    return ReadCdTestDataDir() + "/videocd-xa.bin";
}

static std::vector<u8> ReadRawFixture(void)
{
    std::vector<u8> out;
    FILE *f = fopen(XaBinPath().c_str(), "rb");
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

static CCueBinFileDevice *OpenXaDisc(void)
{
    static const char *cue =
        "FILE \"videocd-xa.bin\" BINARY\n"
        "  TRACK 01 MODE2/2352\n"
        "    INDEX 01 00:00:00\n";

    FIL *fp = new FIL();
    if (f_open(fp, XaBinPath().c_str(), FA_READ) != FR_OK) {
        delete fp;
        return nullptr;
    }
    char *copy = new char[strlen(cue) + 1];
    memcpy(copy, cue, strlen(cue) + 1);
    CCueBinFileDevice *dev = new CCueBinFileDevice(fp, copy, MEDIA_TYPE::CD);
    delete[] copy;
    return dev;
}

// Build a READ CD CDB. sectorType goes in byte 1 bits 4..2, the field
// selection in byte 9.
static void MakeReadCdCdb(u8 *cdb, u8 sectorType, u32 lba, u32 blocks, u8 fields)
{
    memset(cdb, 0, 12);
    cdb[0] = 0xBE;
    cdb[1] = (u8)(sectorType << 2);
    cdb[2] = (u8)(lba >> 24);
    cdb[3] = (u8)(lba >> 16);
    cdb[4] = (u8)(lba >> 8);
    cdb[5] = (u8)lba;
    cdb[6] = (u8)(blocks >> 16);
    cdb[7] = (u8)(blocks >> 8);
    cdb[8] = (u8)blocks;
    cdb[9] = fields;
}

// ---------------------------------------------------------------------------
// The Windows XP failure, reproduced exactly
// ---------------------------------------------------------------------------

TEST(readcd_form2_sync_through_userdata_starts_at_the_sync_pattern)
{
    std::vector<u8> raw = ReadRawFixture();
    CHECK_EQ(raw.size(), (size_t)(kForm1Sectors + kForm2Sectors) * 2352);
    if (raw.empty()) {
        return;
    }

    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Byte 1 = 0x14 (expected sector type 5, Mode 2 Form 2), byte 9 = 0xF0
    // (sync + header + subheader + user data, no EDC/ECC). This is the exact
    // CDB Windows XP sends when copying a Form 2 file, taken off the wire.
    const u32 lba = kForm1Sectors; // first Form 2 sector
    u8 cdb[12];
    MakeReadCdCdb(cdb, 5, lba, 1, 0xF0);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 0);

    // 12 sync + 4 header + 8 subheader + 2324 user data. The old code replied
    // with 2328 bytes taken from offset 24, i.e. the payload where the host
    // asked for the sync pattern, and XP called the file unreadable.
    CHECK_EQ(r.data.size(), (size_t)2348);
    if (r.data.size() != 2348) {
        return;
    }

    // Starts at byte 0 of the sector, so the first 12 bytes are the sync mark.
    CHECK_EQ(r.data[0], 0x00);
    for (int i = 1; i <= 10; i++) {
        CHECK_EQ(r.data[i], 0xFF);
    }
    CHECK_EQ(r.data[11], 0x00);

    // Form 2 is what the fixture holds here: submode bit 0x20 set.
    CHECK((raw[lba * 2352 + 18] & 0x20) == 0x20);

    // Byte-exact against the image, which pins the offset and the length
    // together - a right-sized reply from the wrong place still fails this.
    CHECK_BYTES(r.data.data(), r.data.size(), raw.data() + (size_t)lba * 2352, 2348);
}

TEST(readcd_formless_header_only_fills_the_whole_sector)
{
    std::vector<u8> raw = ReadRawFixture();
    if (raw.empty()) {
        return;
    }
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // The second command Windows XP sends for the same file, taken off the
    // wire: byte 1 = 0x0C (expected type 3, Mode 2 formless), byte 9 = 0xB0.
    // XP issues it even when the Form 2 request above succeeds, so it is part
    // of the sequence rather than a fallback and it has to be answered.
    //
    // Byte 9 bits 6..5 are one two-bit Header Codes value, not two flags:
    // 00 none, 01 header only, 10 subheader only, 11 both. 0xB0 is 01, so it
    // asks for the 4-byte header, not the subheader. A formless Mode 2 sector
    // has no subheader anyway, so this is sync + header + 2336 bytes of data:
    // the whole 2352-byte sector, which is exactly what XP allocates for it.
    //
    // Reading those two bits in bit order rather than as a value makes this
    // look like it skips the header and lands in the non-contiguous branch.
    const u32 lba = kForm1Sectors;
    u8 cdb[12];
    MakeReadCdCdb(cdb, 3, lba, 1, 0xB0);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)2352);
    CHECK_EQ(r.csw.dCSWDataResidue, 0u);
    if (r.data.size() != 2352) {
        return;
    }
    CHECK_EQ(r.data[0], 0x00);
    CHECK_EQ(r.data[1], 0xFF);
    CHECK_BYTES(r.data.data(), r.data.size(), raw.data() + (size_t)lba * 2352, 2352);
}

// ---------------------------------------------------------------------------
// The field selection itself
// ---------------------------------------------------------------------------

TEST(readcd_user_data_only_returns_user_data_not_ecc)
{
    std::vector<u8> raw = ReadRawFixture();
    if (raw.empty()) {
        return;
    }
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Byte 9 = 0x10 is "user data only", the most ordinary request there is.
    // It becomes selection 0x02, which the old mapping read as EDC/ECC: the
    // reply was 288 bytes of error-correction data with a residue of 2064.
    //
    // Both expected sector types matter and they failed differently. Type 0
    // (unspecified) is the one that went through the mis-mapped selection and
    // returned the ECC. Type 4 took a branch that ignored byte 9 entirely and
    // happened to hardcode the right answer, so it passed for the wrong reason
    // - worth pinning so it cannot regress when the hardcoding is gone.
    const u32 lba = 16; // Form 1, and the Video CD's volume descriptor
    for (u8 sectorType : {(u8)0, (u8)4}) {
        u8 cdb[12];
        MakeReadCdCdb(cdb, sectorType, lba, 1, 0x10);

        auto r = bench.SendCommand(cdb, sizeof(cdb), 2048);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK_EQ(r.data.size(), (size_t)2048);
        CHECK_EQ(r.csw.dCSWDataResidue, 0u);
        if (r.data.size() != 2048) {
            continue;
        }

        // Independent oracle: user data of LBA 16 on this disc is the ISO 9660
        // primary volume descriptor, so it begins 0x01 "CD001".
        CHECK_EQ(r.data[0], 0x01);
        CHECK(memcmp(r.data.data() + 1, "CD001", 5) == 0);
        CHECK_BYTES(r.data.data(), r.data.size(), raw.data() + (size_t)lba * 2352 + 24, 2048);
    }
}

TEST(readcd_form2_user_data_only_is_2324_bytes)
{
    std::vector<u8> raw = ReadRawFixture();
    if (raw.empty()) {
        return;
    }
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // The whole point of READ CD for these files: a Form 2 sector's user data
    // is 2324 bytes, not 2048. Serving 2048 silently drops 276 bytes of real
    // payload from every sector.
    const u32 lba = kForm1Sectors;
    u8 cdb[12];
    MakeReadCdCdb(cdb, 5, lba, 1, 0x10);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)2324);
    if (r.data.size() != 2324) {
        return;
    }
    CHECK_BYTES(r.data.data(), r.data.size(), raw.data() + (size_t)lba * 2352 + 24, 2324);
}

TEST(readcd_raw_all_fields_is_unchanged)
{
    std::vector<u8> raw = ReadRawFixture();
    if (raw.empty()) {
        return;
    }
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Selection 0xF8 sets every bit, so it always summed to 2352 even with the
    // fields mis-mapped. It is the mode Windows 98 uses for Mode 2 files and
    // the only one that ever worked, so it is the regression that matters most:
    // whatever else changes, this reply must not.
    for (u32 lba : {(u32)16, kForm1Sectors}) {
        u8 cdb[12];
        MakeReadCdCdb(cdb, 0, lba, 1, 0xF8);
        auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
        CHECK_EQ(r.csw.bmCSWStatus, 0);
        CHECK_EQ(r.data.size(), (size_t)2352);
        CHECK_EQ(r.csw.dCSWDataResidue, 0u);
        if (r.data.size() == 2352) {
            CHECK_BYTES(r.data.data(), r.data.size(), raw.data() + (size_t)lba * 2352, 2352);
        }
    }
}

TEST(readcd_multiple_blocks_keep_the_selected_layout)
{
    std::vector<u8> raw = ReadRawFixture();
    if (raw.empty()) {
        return;
    }
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // XP asks for one sector and then five more in a single command. The
    // per-sector stride has to stay at the selected length, not the 2352 the
    // sector occupies in the image.
    const u32 lba = kForm1Sectors + 1;
    const u32 blocks = 5;
    u8 cdb[12];
    MakeReadCdCdb(cdb, 5, lba, blocks, 0xF0);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2348 * blocks);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)2348 * blocks);
    if (r.data.size() != (size_t)2348 * blocks) {
        return;
    }
    for (u32 i = 0; i < blocks; i++) {
        CHECK_BYTES(r.data.data() + (size_t)i * 2348, 2348,
                    raw.data() + (size_t)(lba + i) * 2352, 2348);
    }
}

TEST(readcd_non_contiguous_selection_is_rejected)
{
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // Sync and user data but not the header and subheader between them. That
    // is not one slice of the sector, and answering with the bytes in between
    // would be worse than refusing.
    u8 cdb[12];
    MakeReadCdCdb(cdb, 4, kForm1Sectors, 1, 0x90);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 1);

    auto sense = bench.RequestSense();
    CHECK_EQ(sense.csw.bmCSWStatus, 0);
    if (sense.data.size() >= 14) {
        CHECK_EQ(sense.data[2] & 0x0F, 0x05); // ILLEGAL REQUEST
        CHECK_EQ(sense.data[12], 0x24);       // INVALID FIELD IN CDB
    }
}

TEST(readcd_empty_selection_is_rejected)
{
    CCueBinFileDevice *disc = OpenXaDisc();
    CHECK(disc != nullptr);
    if (!disc) {
        return;
    }
    CGadgetTestBench bench(disc);
    bench.Activate();
    bench.RequestSense();

    // No fields at all. The block count divides by the transfer size, so this
    // has to be refused rather than allowed to divide by zero.
    u8 cdb[12];
    MakeReadCdCdb(cdb, 4, 16, 1, 0x00);

    auto r = bench.SendCommand(cdb, sizeof(cdb), 2352);
    CHECK_EQ(r.csw.bmCSWStatus, 1);

    auto sense = bench.RequestSense();
    if (sense.data.size() >= 14) {
        CHECK_EQ(sense.data[2] & 0x0F, 0x05);
        CHECK_EQ(sense.data[12], 0x24);
    }
}
