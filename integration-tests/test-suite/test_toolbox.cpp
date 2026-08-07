//
// test_toolbox.cpp
//
// The vendor "SCSI Toolbox" command set (0xD0/0xD2/0xD7/0xD8/0xD9), plus the
// send-file commands (0xD3/0xD4/0xD5) that carry a Wi-Fi configuration.
//
// This is USBODE's signature feature: it is how the DOS/host-side picker
// enumerates the images on the SD card and swaps the disc without touching
// the Pi. The commands are vendor-specific, so there is no standard the
// client can fall back on -- the client parses these bytes at fixed offsets,
// and any drift in the wire format silently breaks disc switching on
// hardware while every standards-defined command keeps working.
//
// These tests pin the format: the fixed device list, the one-byte count with
// its 100-entry cap, and the 40-byte directory entry with its 40-bit
// big-endian size field.
//
// NOTE: one test is deliberately held out of this file --
// toolbox_list_files_name_padding_is_zeroed. LIST FILES allocates its entry
// array with plain new[] and writes only the name characters and a
// terminator, so every byte between an entry's NUL and its size field is
// uninitialized heap that goes out on the wire. The response therefore
// differs between two otherwise identical calls, which is both a small
// information leak and a source of flakiness. The held-out test asserts that
// every byte between an entry's NUL and its size field is zero, and fails
// today: about one run in three with a two-entry catalog. It belongs with the
// one-line fix -- `new TUSBCDToolboxFileEntry[MAX_ENTRIES]()` -- on the
// firmware branch rather than here. Until then CheckEntry() below stops at
// the NUL rather than pinning heap contents.
//
#include "bench.h"
#include "fatfs_host.h"
#include "framework.h"

#include <circle/logger.h>
#include <configservice/wificonfig.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Check the defined bytes of directory entry `slot`: index, type, the name up
// to and including its NUL, and the 40-bit big-endian size. Indexing at
// slot * 40 pins the entry stride.
//
// The 33-byte name field is deliberately only checked up to the NUL. The
// firmware allocates the entry array uninitialized and writes only the name
// characters plus a terminator, so the padding after the NUL is whatever was
// in the heap and differs from call to call (see the latent-bug note in
// README.md). Asserting on it would make this suite flaky rather than catch
// the bug, so the padding is covered by a held-out test instead.
static void CheckEntry(const std::vector<u8> &data, size_t slot,
                       u8 index, u8 type, const char *name, u64 size)
{
    CHECK(data.size() >= (slot + 1) * 40);
    const u8 *e = data.data() + slot * 40;
    CHECK_EQ(e[0], index);
    CHECK_EQ(e[1], type);

    size_t len = strlen(name);
    CHECK_BYTES(e + 2, len, (const u8 *)name, len);
    CHECK_EQ(e[2 + len], 0);

    const u8 expectedSize[5] = {(u8)(size >> 32), (u8)(size >> 24),
                                (u8)(size >> 16), (u8)(size >> 8), (u8)size};
    CHECK_BYTES(e + 35, 5, expectedSize, 5);
}

// A catalog of `count` images, named image00.iso, image01.iso, ... with
// distinct sizes so a field mix-up shows up as a mismatch rather than a
// coincidence.
static void FillCatalog(SCSITBService &tb, size_t count)
{
    tb.entries.clear();
    for (size_t i = 0; i < count; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "image%02zu.iso", i);
        tb.entries.push_back({name, (DWORD)(1000 + i * 7)});
    }
}

// Toolbox commands are 10-byte CDBs; only byte 0 (and byte 1 for SET NEXT CD)
// is interpreted.
static CGadgetTestBench::Result Toolbox(CGadgetTestBench &bench, u8 opcode,
                                        u32 expectLength, u8 arg = 0)
{
    const u8 cdb[10] = {opcode, arg, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), expectLength);
}

// LIST DEVICES (0xD9) tells the client which of the eight toolbox device
// slots are populated. USBODE is a CD-ROM in slot 0 and nothing else; a
// client that sees a different code in byte 0 will not talk to the drive at
// all. 0xff means "no device".
TEST(toolbox_list_devices)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD9, 8);
    CHECK_EQ(r.csw.bmCSWStatus, 0);

    const u8 expected[8] = {0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CHECK_BYTES(r.data.data(), r.data.size(), expected, sizeof(expected));
}

// COUNT FILES (0xD2, and 0xDA as an alias) returns a single byte: how many
// images the picker should ask for. The client uses it to size its own list,
// so an off-by-one here truncates the last image or reads a junk entry.
TEST(toolbox_number_of_files)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 5);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD2, 1);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)1);
    CHECK_EQ(r.data[0], 5);

    // 0xDA is wired to the same handler and must answer identically.
    auto alias = Toolbox(bench, 0xDA, 1);
    CHECK_EQ(alias.csw.bmCSWStatus, 0);
    CHECK_BYTES(alias.data.data(), alias.data.size(), r.data.data(), r.data.size());
}

// An SD card with no images is a normal state (fresh card, wrong folder), not
// an error: the count is zero and the command still succeeds, so the picker
// shows an empty list instead of an error.
TEST(toolbox_number_of_files_empty_catalog)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD2, 1);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)1);
    CHECK_EQ(r.data[0], 0);
}

// The count is returned in one byte, so the protocol caps at 100 entries. A
// card holding more must report the cap, not the true count: 300 images
// truncated to a byte would report 44, and the client would then request
// entries the drive never sent.
TEST(toolbox_number_of_files_caps_at_100)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 300);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD2, 1);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data[0], 100);
}

// LIST FILES (0xD0, alias 0xD7) is the directory itself: a packed array of
// 40-byte entries, each index / type / 33-byte NUL-padded name / 40-bit
// big-endian size. The client indexes into this array by offset, so entry
// stride and field placement are load-bearing.
TEST(toolbox_list_files_entry_format)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    tbservice.entries.push_back({"doom.iso", 0x00000001});
    tbservice.entries.push_back({"quake.cue", 0x89ABCDEF});
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD0, 2 * 40);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)80);

    CheckEntry(r.data, 0, 0, 0, "doom.iso", 0x0000000001);
    // 0x0089ABCDEF exercises all four low bytes of the 40-bit size field; the
    // top byte is always zero because the catalog size is 32-bit.
    CheckEntry(r.data, 1, 1, 0, "quake.cue", 0x0089ABCDEF);
}

// The name field holds 32 characters plus a NUL. Real SD cards are full of
// long release names, and a name copied without a cap would run straight
// into the size field of its own entry -- the client would then show a
// garbled name and a wildly wrong file size.
TEST(toolbox_list_files_long_name_truncated)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    // 40 characters, longer than the 32 the entry can hold.
    const std::string longName = "A_Very_Long_Disc_Image_Name_1995_CD1.iso";
    CHECK_EQ(longName.size(), (size_t)40);
    tbservice.entries.push_back({longName, 4242});
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD0, 40);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)40);

    // First 32 characters kept, then a NUL at byte 34 (offset 2 + 32).
    CHECK_BYTES(r.data.data() + 2, 32, (const u8 *)longName.data(), 32);
    CHECK_EQ(r.data[34], 0);

    // The size field is intact rather than overwritten by name bytes.
    CHECK_EQ(r.data[35], 0);
    CHECK_EQ(r.data[36], 0);
    CHECK_EQ(r.data[37], 0);
    CHECK_EQ(r.data[38], (4242 >> 8) & 0xFF);
    CHECK_EQ(r.data[39], 4242 & 0xFF);
}

// A name shorter than the field must still be NUL-terminated, so a client
// reading the name does not run on into the padding.
TEST(toolbox_list_files_short_name_is_terminated)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    tbservice.entries.push_back({"a.iso", 1});
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD0, 40);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)40);
    CheckEntry(r.data, 0, 0, 0, "a.iso", 1);
}

// 0xD7 is wired to the same handler as 0xD0 and must return the same bytes;
// different toolbox clients use different opcodes for this.
TEST(toolbox_list_files_alias_opcode)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 3);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto d0 = Toolbox(bench, 0xD0, 3 * 40);
    auto d7 = Toolbox(bench, 0xD7, 3 * 40);
    CHECK_EQ(d0.csw.bmCSWStatus, 0);
    CHECK_EQ(d7.csw.bmCSWStatus, 0);
    CHECK_EQ(d7.data.size(), d0.data.size());

    // Compared field by field rather than byte by byte: the two responses come
    // from separate allocations, so their name padding need not agree.
    for (size_t i = 0; i < 3; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "image%02zu.iso", i);
        CheckEntry(d0.data, i, (u8)i, 0, name, 1000 + i * 7);
        CheckEntry(d7.data, i, (u8)i, 0, name, 1000 + i * 7);
    }
}

// The directory is capped at the same 100 entries the count is, so the two
// commands agree. A client that trusts the count of 100 and then receives a
// longer array would walk past the end of its own buffer.
TEST(toolbox_list_files_caps_at_100)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 150);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD0, 150 * 40);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)(100 * 40));

    // Last reported entry is index 99, not 149, and it is a real entry rather
    // than a slot the loop never reached.
    CheckEntry(r.data, 99, 99, 0, "image99.iso", 1000 + 99 * 7);
}

// A toolbox command reached with a read still pending must answer with its own
// data and nothing else. Every other data-in handler clears the pending block
// count before its transfer; the toolbox ones did not, so the gadget resumed
// the read when the toolbox transfer completed and streamed disc sectors onto
// the end of the reply. Measured before the fix: LIST DEVICES returning 10248
// bytes instead of 8, being the device list plus five 2048-byte sectors, which
// the picker then parses as its device list. An out-of-range stale LBA fails
// the command instead, so enumeration breaks either way.
//
// Reachable at boot, when the transfer state has not been through a read yet,
// and after any aborted transfer.
TEST(toolbox_command_does_not_resume_a_pending_read)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 3);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    bench.SetPendingBlocks(5);
    auto devices = Toolbox(bench, 0xD9, 8);
    CHECK_EQ(devices.csw.bmCSWStatus, 0);
    CHECK_EQ(devices.data.size(), (size_t)8);

    bench.SetPendingBlocks(5);
    auto count = Toolbox(bench, 0xD2, 1);
    CHECK_EQ(count.csw.bmCSWStatus, 0);
    CHECK_EQ(count.data.size(), (size_t)1);

    bench.SetPendingBlocks(5);
    auto files = Toolbox(bench, 0xD0, 3 * 40);
    CHECK_EQ(files.csw.bmCSWStatus, 0);
    CHECK_EQ(files.data.size(), (size_t)(3 * 40));
}

// The name field is 33 bytes but only each name's characters and its
// terminator are written, so with plain new[] everything after the NUL was
// whatever happened to be in the heap: up to roughly 2 KB of Pi memory per
// full catalog, shipped to any host that asks.
TEST(toolbox_list_files_name_padding_is_zeroed)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 2);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = Toolbox(bench, 0xD0, 2 * 40);
    CHECK_EQ(r.csw.bmCSWStatus, 0);
    CHECK_EQ(r.data.size(), (size_t)80);

    // Everything from each name's NUL up to the size field at offset 35 must
    // be zero rather than heap residue.
    for (size_t slot = 0; slot < 2; slot++)
    {
        const u8 *e = r.data.data() + slot * 40;
        size_t len = strlen((const char *)e + 2);
        for (size_t i = 2 + len + 1; i < 35; i++)
        {
            CHECK_EQ(e[i], 0);
        }
    }
}

// The same catalog asked for twice must come back byte for byte identical.
// This is the assertion that originally failed about one run in three.
TEST(toolbox_list_files_is_deterministic)
{
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 8);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto first = Toolbox(bench, 0xD0, 8 * 40);
    auto second = Toolbox(bench, 0xD0, 8 * 40);
    CHECK_BYTES(second.data.data(), second.data.size(),
                first.data.data(), first.data.size());
}

static const char kOriginalConfig[] =
    "country=GB\n"
    "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
    "network={\n\tssid=\"OldNetwork\"\n\tpsk=\"OldSecret123\"\n}\n";

// Distinctive enough that a log scan for it cannot match by accident.
static const char kPassword[] = "S3cretWiFiPassw0rd";

static std::string WiFiRoot()
{
#ifdef USBODE_TESTDATA
    return std::string(USBODE_TESTDATA) + "/wifiroot";
#else
    return "out/images/wifiroot";
#endif
}

static std::string RootPath(const char *pName)
{
    return WiFiRoot() + "/" + pName;
}

static bool ReadRootFile(const char *pName, std::string &out)
{
    out.clear();
    FILE *f = fopen(RootPath(pName).c_str(), "rb");
    if (f == nullptr)
    {
        return false;
    }
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        out.append(buf, n);
    }
    fclose(f);
    return true;
}

static void WriteRootFile(const char *pName, const std::string &content)
{
    FILE *f = fopen(RootPath(pName).c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr)
    {
        return;
    }
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

// Return the staging singleton to Idle whatever state a previous test left it
// in, so one test's leftovers cannot decide the next one's result.
static void DrainUpload()
{
    CWiFiConfigUpload &upload = CWiFiConfigUpload::Get();
    if (upload.CommitPending())
    {
        upload.ProcessCommit();
    }
    upload.Abort();
    upload.ConsumeRebootRequest();
}

// Points the firmware's "0:/..." paths at a scratch directory and seeds it
// with a working configuration, so "the original survived" is a real check.
struct WiFiFixture
{
    WiFiFixture()
    {
        mkdir(WiFiRoot().c_str(), 0777);
        FatFsHostClearFaults();
        FatFsHostSetDriveRoot(WiFiRoot().c_str());
        DrainUpload();
        Clean();
        WriteRootFile("wpa_supplicant.conf", kOriginalConfig);
    }

    ~WiFiFixture()
    {
        DrainUpload();
        FatFsHostClearFaults();
        Clean();
        FatFsHostSetDriveRoot(nullptr);
    }

    void Clean()
    {
        remove(RootPath("wpa_supplicant.conf").c_str());
        remove(RootPath("wpa_supplicant.tmp").c_str());
        remove(RootPath("wpa_supplicant.bak").c_str());
    }
};

// Exactly nTotal bytes of configuration-shaped text. Truncation may remove
// kPassword, so secrecy tests must verify that their sample contains it.
static std::string MakeConfig(size_t nTotal)
{
    std::string s = "country=US\nctrl_interface=DIR=/var/run/wpa_supplicant\nupdate_config=1\n"
                    "network={\n\tssid=\"HomeNet\"\n\tpsk=\"";
    s += kPassword;
    s += "\"\n}\n";
    while (s.size() < nTotal)
    {
        s += "# padding so the upload spans more than one block\n";
    }
    s.resize(nTotal);
    return s;
}

// TOOLBOX_SEND_FILE_PREP. The client declares 33 bytes and NUL-terminates the
// name inside them; nSupplied under nDeclared models a short data phase.
static CGadgetTestBench::Result SendFilePrep(CGadgetTestBench &bench, const char *pName,
                                             u32 nDeclared = 33, size_t nSupplied = 33)
{
    u8 payload[64];
    memset(payload, 0, sizeof(payload));
    size_t len = strlen(pName);
    if (len > 32)
    {
        len = 32;
    }
    memcpy(payload, pName, len);

    const u8 cdb[10] = {0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), nDeclared, false, payload, nSupplied);
}

// The same command with the parameter list handed over verbatim, for names
// that are not C strings (no terminator, embedded control bytes).
static CGadgetTestBench::Result SendFilePrepRaw(CGadgetTestBench &bench, const u8 *pName,
                                                size_t nNameLength, u32 nDeclared = 33)
{
    const u8 cdb[10] = {0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), nDeclared, false, pName, nNameLength);
}

// TOOLBOX_SEND_FILE_10. The client always moves 512 bytes and says in the CDB
// how many of them are real; the filler here is what must never be written.
static CGadgetTestBench::Result SendFileBlock(CGadgetTestBench &bench, u32 nBlockIndex,
                                              const void *pData, u16 nValidLength,
                                              u32 nDeclared = 512, size_t nSupplied = 512)
{
    u8 payload[512];
    memset(payload, 0xAA, sizeof(payload));
    size_t copy = nValidLength;
    if (copy > sizeof(payload))
    {
        copy = sizeof(payload);
    }
    if (pData != nullptr && copy > 0)
    {
        memcpy(payload, pData, copy);
    }

    const u8 cdb[10] = {0xD4,
                        (u8)(nValidLength >> 8), (u8)(nValidLength & 0xFF),
                        (u8)((nBlockIndex >> 16) & 0xFF), (u8)((nBlockIndex >> 8) & 0xFF),
                        (u8)(nBlockIndex & 0xFF),
                        0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), nDeclared, false, payload, nSupplied);
}

// TOOLBOX_SEND_FILE_END. nDeclared 4 is what the DOS client sends; 0 is the
// documented no-payload form.
static CGadgetTestBench::Result SendFileEnd(CGadgetTestBench &bench, u32 nDeclared = 4)
{
    const u8 payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    const u8 cdb[10] = {0xD5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return bench.SendCommand(cdb, sizeof(cdb), nDeclared, false,
                             nDeclared > 0 ? payload : nullptr,
                             nDeclared > 0 ? sizeof(payload) : 0);
}

static bool UploadConfig(CGadgetTestBench &bench, const char *pName,
                         const std::string &content, u32 nEndLength = 4)
{
    if (SendFilePrep(bench, pName).csw.bmCSWStatus != 0)
    {
        return false;
    }
    size_t blocks = (content.size() + 511) / 512;
    for (size_t i = 0; i < blocks; i++)
    {
        size_t off = i * 512;
        size_t n = content.size() - off;
        if (n > 512)
        {
            n = 512;
        }
        if (SendFileBlock(bench, (u32)i, content.data() + off, (u16)n).csw.bmCSWStatus != 0)
        {
            return false;
        }
    }
    return SendFileEnd(bench, nEndLength).csw.bmCSWStatus == 0;
}

// Sense key / ASC / ASCQ of the last failure, read the way a host reads it.
static void CheckSense(CGadgetTestBench &bench, u8 key, u8 asc, u8 ascq)
{
    auto sense = bench.RequestSense();
    CHECK_EQ(sense.data.size() >= (size_t)14, true);
    if (sense.data.size() < 14)
    {
        return;
    }
    CHECK_EQ(sense.data[2] & 0x0F, key);
    CHECK_EQ(sense.data[12], asc);
    CHECK_EQ(sense.data[13], ascq);
}

// 700 bytes is two blocks with a 188-byte final one: the shape of every real
// upload, and it has to land in 0:/wpa_supplicant.conf byte for byte.
TEST(sendfile_uploads_a_wifi_config)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(700);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config));

    // Nothing has touched the SD card yet: the commit is queued for the task
    // context, and the reboot waits on the commit.
    CHECK(CWiFiConfigUpload::Get().CommitPending());
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);

    CWiFiConfigUpload::Get().ProcessCommit();
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_EQ(onDisk.size(), config.size());
    CHECK(onDisk == config);
    CHECK(CWiFiConfigUpload::Get().ConsumeRebootRequest());

    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);
    CHECK(!CWiFiConfigUpload::Get().CommitPending());
}

// The lengths the DOS client really declares (33 / 512 / 4). Accepting only
// the documented shapes would fail against the shipping client.
TEST(sendfile_accepts_the_dos_client_transfer_lengths)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(100);

    auto prep = SendFilePrep(bench, "wpa_supplicant.conf", 33, 33);
    CHECK_EQ(prep.csw.bmCSWStatus, 0);
    CHECK_EQ(prep.csw.dCSWDataResidue, 0u);

    auto block = SendFileBlock(bench, 0, config.data(), 100, 512, 512);
    CHECK_EQ(block.csw.bmCSWStatus, 0);
    CHECK_EQ(block.csw.dCSWDataResidue, 0u);

    auto end = SendFileEnd(bench, 4);
    CHECK_EQ(end.csw.bmCSWStatus, 0);
    CHECK_EQ(end.csw.dCSWDataResidue, 0u);

    CWiFiConfigUpload::Get().ProcessCommit();
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
}

// toolbox.h documents SEND_FILE_END as taking no data at all. Supporting both
// keeps any client that follows the document working.
TEST(sendfile_end_accepts_the_documented_no_payload_form)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(64);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config, 0));

    CWiFiConfigUpload::Get().ProcessCommit();
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
}

// WIFI.CFG is the other name the tooling uses, and DOS clients upper-case
// their filenames, so the match has to be case insensitive.
TEST(sendfile_accepts_wifi_cfg_case_insensitively)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(64);
    CHECK(UploadConfig(bench, "WIFI.CFG", config));
    CWiFiConfigUpload::Get().ProcessCommit();

    // Whichever name was uploaded, the bytes land in wpa_supplicant.conf.
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
    CHECK(!ReadRootFile("WIFI.CFG", onDisk));

    CGadgetTestBench bench2(disc, false, nullptr, nullptr, &tbservice);
    bench2.Activate();
    bench2.RequestSense();
    CHECK(UploadConfig(bench2, "Wpa_Supplicant.CONF", config));
    CWiFiConfigUpload::Get().ProcessCommit();
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
}

// The block index is an absolute position, not an append cursor: a client
// retrying a block after a bus reset must not lengthen the file.
TEST(sendfile_places_blocks_absolutely_and_retries_in_place)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    std::string block0(512, 'A');
    std::string block1(200, 'B');
    std::string retry1(200, 'C');

    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, block0.data(), 512).csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 1, block1.data(), 200).csw.bmCSWStatus, 0);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 712u);

    CHECK_EQ(SendFileBlock(bench, 1, retry1.data(), 200).csw.bmCSWStatus, 0);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 712u);

    std::string rewrite0(512, 'D');
    CHECK_EQ(SendFileBlock(bench, 0, rewrite0.data(), 512).csw.bmCSWStatus, 0);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 712u);

    CHECK_EQ(SendFileEnd(bench).csw.bmCSWStatus, 0);
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_EQ(onDisk.size(), (size_t)712);
    CHECK(onDisk == rewrite0 + retry1);
}

// The final block still moves 512 bytes; only the CDB count is the file.
// Writing the padding would append hundreds of junk bytes to every config.
TEST(sendfile_partial_block_writes_only_the_declared_bytes)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(300);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config));
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_EQ(onDisk.size(), (size_t)300);
    CHECK(onDisk.find('\xAA') == std::string::npos);
    CHECK(onDisk == config);
}

// A block with no PREP behind it has nowhere to go. Accepting it would mean
// the staging buffer's contents came from an unknown command sequence.
TEST(sendfile_block_without_prep_fails_closed)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    std::string data(64, 'x');
    auto r = SendFileBlock(bench, 0, data.data(), 64);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CHECK_EQ(r.csw.dCSWDataResidue, 512u);
    CHECK(r.stalledOut);
    CheckSense(bench, 0x05, 0x2c, 0x00); // COMMAND SEQUENCE ERROR

    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
}

// Likewise END: with nothing staged there is nothing to install, and a commit
// of an empty buffer would truncate a working configuration to zero bytes.
TEST(sendfile_end_without_prep_fails_closed)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    auto r = SendFileEnd(bench);
    CHECK_EQ(r.csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x2c, 0x00);
    CHECK(!CWiFiConfigUpload::Get().CommitPending());

    // The no-payload form has to fail the same way rather than commit nothing.
    auto documented = SendFileEnd(bench, 0);
    CHECK_EQ(documented.csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x2c, 0x00);
    CHECK(!CWiFiConfigUpload::Get().CommitPending());

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
}

// A PREP mid-transfer means the client restarted; the first attempt's tail
// must not survive under a shorter second upload.
TEST(sendfile_second_prep_abandons_the_first_upload)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    std::string abandoned(512, 'Z');
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, abandoned.data(), 512).csw.bmCSWStatus, 0);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 512u);

    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);

    const std::string config = MakeConfig(40);
    CHECK_EQ(SendFileBlock(bench, 0, config.data(), 40).csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileEnd(bench).csw.bmCSWStatus, 0);
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_EQ(onDisk.size(), (size_t)40);
    CHECK(onDisk == config);

    // A rejected second PREP still abandons the first: the client asked for a
    // different destination, so what it staged before is meaningless.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, abandoned.data(), 512).csw.bmCSWStatus, 0);
    CHECK_EQ(SendFilePrep(bench, "autoexec.bat").csw.bmCSWStatus, 1);
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);
    CHECK(!CWiFiConfigUpload::Get().IsReceiving());
}

// 8 KiB is the documented cap. A block index past it is refused before
// anything is copied, so the overflow never reaches the staging buffer.
TEST(sendfile_oversized_upload_is_refused)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    std::string block(512, 'q');
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);

    // Blocks 0..15 fill the buffer exactly; block 16 is one too many.
    for (u32 i = 0; i < 16; i++)
    {
        CHECK_EQ(SendFileBlock(bench, i, block.data(), 512).csw.bmCSWStatus, 0);
    }
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 8192u);

    auto over = SendFileBlock(bench, 16, block.data(), 512);
    CHECK_EQ(over.csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x24, 0x00); // INVALID FIELD IN CDB
    CHECK(!CWiFiConfigUpload::Get().IsReceiving());

    // The rejected block took the whole upload with it, so END has nothing.
    CHECK_EQ(SendFileEnd(bench).csw.bmCSWStatus, 1);
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);

    // The largest 24-bit index the CDB can hold must not overflow into a
    // usable offset either.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0xFFFFFF, block.data(), 512).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x24, 0x00);
}

// A gap would leave NUL bytes mid-configuration. The client is strictly
// sequential, so a jump forward is malformed, not a sparse write.
TEST(sendfile_refuses_a_gap_between_blocks)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    std::string block(512, 'g');
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, block.data(), 512).csw.bmCSWStatus, 0);

    auto gap = SendFileBlock(bench, 3, block.data(), 512);
    CHECK_EQ(gap.csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x24, 0x00);
    CHECK(!CWiFiConfigUpload::Get().IsReceiving());
}

// Not a general file transfer: every name but the two Wi-Fi ones is refused,
// including the traversal and drive-prefix shapes that reach the rest of the card.
TEST(sendfile_rejects_every_other_destination)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const char *rejected[] = {
        "config.txt",
        "image.iso",
        "wpa_supplicant.con",
        "wpa_supplicant.confx",
        "../wpa_supplicant.conf",
        "..\\wpa_supplicant.conf",
        "/wpa_supplicant.conf",
        "0:/wpa_supplicant.conf",
        "C:\\wpa_supplicant.conf",
        "subdir/wpa_supplicant.conf",
        "..",
        "",
    };

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
    {
        auto r = SendFilePrep(bench, rejected[i]);
        CHECK_EQ(r.csw.bmCSWStatus, 1);
        CheckSense(bench, 0x05, 0x26, 0x00); // INVALID FIELD IN PARAMETER LIST
        CHECK(!CWiFiConfigUpload::Get().IsReceiving());
    }

    // A name with an embedded control byte, and one with no terminator at all
    // inside the 33-byte field.
    u8 control[33];
    memset(control, 0, sizeof(control));
    memcpy(control, "wpa_supplicant\x01.conf", 19);
    CHECK_EQ(SendFilePrepRaw(bench, control, sizeof(control)).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x26, 0x00);

    u8 unterminated[33];
    memset(unterminated, 'A', sizeof(unterminated));
    CHECK_EQ(SendFilePrepRaw(bench, unterminated, sizeof(unterminated)).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x26, 0x00);

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
}

// Lengths that describe no performable transfer are refused before any buffer
// is read, so the handler cannot walk past what actually arrived.
TEST(sendfile_rejects_bad_transfer_lengths)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf", 0, 0).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x1a, 0x00); // PARAMETER LIST LENGTH ERROR
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf", 64, 64).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x1a, 0x00);

    // A PREP whose data phase is cut short before the name's terminator.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf", 33, 5).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x26, 0x00);

    std::string data(512, 'y');
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);

    CHECK_EQ(SendFileBlock(bench, 0, data.data(), 0).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x24, 0x00);

    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, data.data(), 513, 512, 512).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x24, 0x00);

    // A CDB claiming more valid bytes than the host actually moved.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    auto shortPhase = SendFileBlock(bench, 0, data.data(), 512, 512, 100);
    CHECK_EQ(shortPhase.csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x1a, 0x00);
    CHECK(!CWiFiConfigUpload::Get().IsReceiving());

    // A declared transfer larger than the gadget's OUT buffer.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, data.data(), 512, 4096).csw.bmCSWStatus, 1);
    CheckSense(bench, 0x05, 0x1a, 0x00);

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
}

// An upload the client never finished must leave the working configuration
// exactly as it was, not a truncated version of the new one.
TEST(sendfile_interrupted_transfer_leaves_the_original_intact)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(900);
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, config.data(), 512).csw.bmCSWStatus, 0);
    // ...and the client goes away here: no second block, no END.

    CHECK(!CWiFiConfigUpload::Get().CommitPending());
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_BYTES(onDisk.data(), onDisk.size(), kOriginalConfig, strlen(kOriginalConfig));

    // No temporary file was left behind for the next boot to trip over.
    std::string leftover;
    CHECK(!ReadRootFile("wpa_supplicant.tmp", leftover));
}

// A full card reports FR_OK with a short write count. Trusting the result code
// would install a truncated configuration and lose the working one.
TEST(sendfile_short_write_does_not_replace_the_original)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(600);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config));

    FatFsHostSetWriteLimit(100);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());

    std::string leftover;
    CHECK(!ReadRootFile("wpa_supplicant.tmp", leftover));
    CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);
}

// Bytes that reached the FAT cache but not the card are not committed. Without
// the sync check a power cut right after the rename loses both files.
TEST(sendfile_sync_failure_does_not_replace_the_original)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    CHECK(UploadConfig(bench, "wpa_supplicant.conf", MakeConfig(200)));

    FatFsHostFailSync(true);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());

    std::string leftover;
    CHECK(!ReadRootFile("wpa_supplicant.tmp", leftover));
}

// FAT cannot replace a file atomically. Both renames are failed in turn here,
// because moving the old file aside and installing the new one roll back differently.
TEST(sendfile_rename_failure_restores_the_original)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    // First rename: the old file cannot be moved out of the way at all.
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", MakeConfig(200)));
    FatFsHostFailRename(1);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());
    std::string leftover;
    CHECK(!ReadRootFile("wpa_supplicant.tmp", leftover));

    // Second rename: the old file is already aside, so the rollback has to
    // bring it back rather than leave the drive with no configuration.
    CGadgetTestBench bench2(disc, false, nullptr, nullptr, &tbservice);
    bench2.Activate();
    bench2.RequestSense();
    CHECK(UploadConfig(bench2, "wpa_supplicant.conf", MakeConfig(200)));
    FatFsHostFailRename(2);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == kOriginalConfig);
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());
    CHECK(!ReadRootFile("wpa_supplicant.tmp", leftover));
}

// Only the documented no-payload form and the DOS client's four bytes are
// accepted; any other length is a client this device has not been proven with.
TEST(sendfile_end_rejects_every_other_payload_length)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const u32 rejected[] = {1, 5, 512};
    const std::string config = MakeConfig(200);

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
    {
        const u32 declared = rejected[i];
        CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
        CHECK_EQ(SendFileBlock(bench, 0, config.data(), 200).csw.bmCSWStatus, 0);
        CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 200u);

        auto r = SendFileEnd(bench, declared);
        CHECK_EQ(r.csw.bmCSWStatus, 1);
        CHECK_EQ(r.csw.dCSWDataResidue, declared);
        CHECK(r.stalledOut);
        CheckSense(bench, 0x05, 0x1a, 0x00); // PARAMETER LIST LENGTH ERROR

        CHECK(!CWiFiConfigUpload::Get().IsReceiving());
        CHECK(!CWiFiConfigUpload::Get().CommitPending());
        CHECK_EQ(CWiFiConfigUpload::Get().StagedLength(), 0u);

        std::string onDisk;
        CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
        CHECK_BYTES(onDisk.data(), onDisk.size(), kOriginalConfig, strlen(kOriginalConfig));
    }

    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config, 4));
    CWiFiConfigUpload::Get().ProcessCommit();
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config, 0));
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
}

// The staging buffer is erased after a commit, so a later shorter upload
// cannot carry a tail of the previous configuration's password onto the card.
TEST(sendfile_commit_leaves_no_residue_for_the_next_upload)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string first = MakeConfig(1200);
    CHECK(first.find(kPassword) != std::string::npos);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", first));
    CWiFiConfigUpload::Get().ProcessCommit();

    const std::string second = "country=US\nnetwork={\n\tssid=\"Other\"\n}\n";
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", second));
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK_EQ(onDisk.size(), second.size());
    CHECK(onDisk == second);
    CHECK(onDisk.find(kPassword) == std::string::npos);
}

// If the rollback fails too, the old configuration exists only under the
// backup name; the next attempt must not clear it before one has succeeded.
TEST(sendfile_failed_rollback_keeps_the_original_as_a_backup)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    // Install and rollback both fail: the old file is stranded at its backup
    // name, and no new configuration is installed.
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", MakeConfig(200)));
    FatFsHostFailRename(2, 2);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    std::string onDisk;
    std::string backup;
    CHECK(!ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(ReadRootFile("wpa_supplicant.bak", backup));
    CHECK(backup == kOriginalConfig);
    CHECK(!CWiFiConfigUpload::Get().ConsumeRebootRequest());
    CHECK(!ReadRootFile("wpa_supplicant.tmp", onDisk));

    // A further failed attempt must leave that rescued copy alone: it is the
    // only configuration the card still has.
    CGadgetTestBench bench2(disc, false, nullptr, nullptr, &tbservice);
    bench2.Activate();
    bench2.RequestSense();
    CHECK(UploadConfig(bench2, "wpa_supplicant.conf", MakeConfig(200)));
    FatFsHostFailRename(1, 1);
    CWiFiConfigUpload::Get().ProcessCommit();
    FatFsHostClearFaults();

    CHECK(ReadRootFile("wpa_supplicant.bak", backup));
    CHECK(backup == kOriginalConfig);
    CHECK(!ReadRootFile("wpa_supplicant.conf", onDisk));

    // Once an install does succeed the backup has been superseded and goes.
    CGadgetTestBench bench3(disc, false, nullptr, nullptr, &tbservice);
    bench3.Activate();
    bench3.RequestSense();
    const std::string config = MakeConfig(120);
    CHECK(UploadConfig(bench3, "wpa_supplicant.conf", config));
    CWiFiConfigUpload::Get().ProcessCommit();

    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
    CHECK(!ReadRootFile("wpa_supplicant.bak", backup));
    CHECK(CWiFiConfigUpload::Get().ConsumeRebootRequest());
}

// A first-time setup has no configuration to move aside. The missing file must
// read as "nothing to back up", not as a failure.
TEST(sendfile_installs_when_no_configuration_exists_yet)
{
    WiFiFixture fixture;
    fixture.Clean();

    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(120);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", config));
    CWiFiConfigUpload::Get().ProcessCommit();

    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
    CHECK(CWiFiConfigUpload::Get().ConsumeRebootRequest());
}

// Routed wrongly, ProcessOut() reads an upload as a mode page. This block is
// shaped exactly like the CD audio control page that moves the volume.
TEST(sendfile_payload_never_reaches_the_mode_select_parser)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CCDPlayer player;
    CGadgetTestBench bench(disc, false, &player, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    u8 payload[512];
    memset(payload, 0, sizeof(payload));
    payload[8] = 0x0E;      // page code: CD audio control
    payload[8 + 1] = 0x0E;  // page length
    payload[8 + 9] = 0x55;  // output 0 volume
    payload[8 + 11] = 0x55; // output 1 volume

    const u8 volumeBefore = player.volume;
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, payload, 512).csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileEnd(bench).csw.bmCSWStatus, 0);

    CHECK_EQ(player.setVolumeCalls, 0);
    CHECK_EQ(player.volume, volumeBefore);
}

// Same rule the other toolbox commands follow: a read left pending by an
// aborted transfer must not resume and stream sectors into the upload.
TEST(sendfile_does_not_resume_a_pending_read)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    const std::string config = MakeConfig(64);

    bench.SetPendingBlocks(5);
    auto prep = SendFilePrep(bench, "wpa_supplicant.conf");
    CHECK_EQ(prep.csw.bmCSWStatus, 0);
    CHECK_EQ(prep.data.size(), (size_t)0);

    bench.SetPendingBlocks(5);
    auto block = SendFileBlock(bench, 0, config.data(), 64);
    CHECK_EQ(block.csw.bmCSWStatus, 0);
    CHECK_EQ(block.data.size(), (size_t)0);

    bench.SetPendingBlocks(5);
    auto end = SendFileEnd(bench);
    CHECK_EQ(end.csw.bmCSWStatus, 0);
    CHECK_EQ(end.data.size(), (size_t)0);

    CWiFiConfigUpload::Get().ProcessCommit();
    std::string onDisk;
    CHECK(ReadRootFile("wpa_supplicant.conf", onDisk));
    CHECK(onDisk == config);
}

// The staged bytes are a Wi-Fi password. Nothing on this path may put them in
// the log, which USBODE writes to the SD card and users attach to bug reports.
TEST(sendfile_never_logs_the_payload)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    ConfigService config;
    config.debugCdrom = true; // the loudest the gadget ever gets
    CGadgetTestBench bench(disc, false, nullptr, &config, &tbservice);
    bench.Activate();
    bench.RequestSense();

    CLogger::TestClearEvents();

    const std::string content = MakeConfig(700);
    CHECK(content.find(kPassword) != std::string::npos);
    CHECK(UploadConfig(bench, "wpa_supplicant.conf", content));
    CWiFiConfigUpload::Get().ProcessCommit();

    // A rejected upload is just as sensitive: the client may well have sent
    // the real password before the name was refused.
    CHECK_EQ(SendFilePrep(bench, "wpa_supplicant.conf").csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 0, content.data(), 512).csw.bmCSWStatus, 0);
    CHECK_EQ(SendFileBlock(bench, 9, content.data(), 512).csw.bmCSWStatus, 1);

    bool bSawUploadLine = false;
    TLogSeverity severity;
    char source[LOG_MAX_SOURCE];
    char message[LOG_MAX_MESSAGE];
    while (CLogger::Get()->ReadEvent(&severity, source, message, nullptr, nullptr, nullptr))
    {
        std::string text(message);
        CHECK(text.find(kPassword) == std::string::npos);
        CHECK(text.find("HomeNet") == std::string::npos);
        CHECK(text.find("ctrl_interface") == std::string::npos);
        if (text.find("wpa_supplicant.conf") != std::string::npos)
        {
            bSawUploadLine = true;
        }
    }

    // The accepted destination is logged, so an operator can see what happened.
    CHECK(bSawUploadLine);
}

// Listing and disc selection share the gadget's OUT buffer and sense data with
// the upload path, so they are re-run afterwards to show nothing was left behind.
TEST(sendfile_leaves_the_other_toolbox_commands_working)
{
    WiFiFixture fixture;
    CFakeImageDevice *disc = MakeDataISO(1200);
    SCSITBService tbservice;
    FillCatalog(tbservice, 3);
    CGadgetTestBench bench(disc, false, nullptr, nullptr, &tbservice);
    bench.Activate();
    bench.RequestSense();

    CHECK(UploadConfig(bench, "wpa_supplicant.conf", MakeConfig(600)));
    CWiFiConfigUpload::Get().ProcessCommit();

    auto devices = Toolbox(bench, 0xD9, 8);
    CHECK_EQ(devices.csw.bmCSWStatus, 0);
    const u8 expected[8] = {0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CHECK_BYTES(devices.data.data(), devices.data.size(), expected, sizeof(expected));

    auto count = Toolbox(bench, 0xD2, 1);
    CHECK_EQ(count.csw.bmCSWStatus, 0);
    CHECK_EQ(count.data[0], 3);

    auto files = Toolbox(bench, 0xD0, 3 * 40);
    CHECK_EQ(files.csw.bmCSWStatus, 0);
    CHECK_EQ(files.data.size(), (size_t)(3 * 40));
    CheckEntry(files.data, 1, 1, 0, "image01.iso", 1007);

    auto select = Toolbox(bench, 0xD8, 0, 2);
    CHECK_EQ(select.csw.bmCSWStatus, 0);
    CHECK_EQ(tbservice.lastSetNextCD, 2);
}
