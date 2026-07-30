//
// test_fatfsseam.cpp
//
// The host backend for FatFs itself. Everything else in the suite trusts this
// seam to behave like the real thing, so where stdio and FatFs disagree the
// difference is pinned here rather than discovered as a firmware "bug" that
// only reproduces on the Pi.
//
#include "framework.h"
#include "fatfs_host.h"

#include <fatfs/ff.h>

#include <stdio.h>
#include <string.h>

#include <string>

static std::string TestDataDir()
{
#ifdef USBODE_TESTDATA
    return USBODE_TESTDATA;
#else
    return "out/images";
#endif
}

// stdio has no "create only if absent" mode, so the shim reached for "r+b",
// which opens the existing file the caller asked to be protected from.
TEST(fatfs_seam_create_new_refuses_an_existing_file)
{
    const std::string path = TestDataDir() + "/seam-create-new.bin";
    remove(path.c_str());

    FIL File;
    CHECK_EQ(f_open(&File, path.c_str(), FA_CREATE_NEW | FA_WRITE), FR_OK);
    UINT nWritten = 0;
    CHECK_EQ(f_write(&File, "first", 5, &nWritten), FR_OK);
    CHECK_EQ(nWritten, 5u);
    f_close(&File);

    // Second time round the file is there, so this must fail rather than
    // handing back a writable handle to it.
    CHECK_EQ(f_open(&File, path.c_str(), FA_CREATE_NEW | FA_WRITE), FR_EXIST);

    // And the original contents survived the refused open.
    CHECK_EQ(f_open(&File, path.c_str(), FA_READ), FR_OK);
    char buf[8] = {0};
    UINT nRead = 0;
    CHECK_EQ(f_read(&File, buf, 5, &nRead), FR_OK);
    CHECK_EQ(nRead, 5u);
    CHECK(memcmp(buf, "first", 5) == 0);
    f_close(&File);

    remove(path.c_str());
}

// The fault hooks the write-path tests depend on: a full card is FR_OK with a
// short count, which is the trap, not an error return.
TEST(fatfs_seam_reports_a_full_card_as_a_short_write)
{
    const std::string path = TestDataDir() + "/seam-full-card.bin";
    remove(path.c_str());

    FIL File;
    CHECK_EQ(f_open(&File, path.c_str(), FA_CREATE_ALWAYS | FA_WRITE), FR_OK);

    FatFsHostSetWriteLimit(4);

    UINT nWritten = 0;
    CHECK_EQ(f_write(&File, "0123456789", 10, &nWritten), FR_OK);
    CHECK_EQ(nWritten, 4u);

    // Nothing more fits, and it is still not an error.
    nWritten = 99;
    CHECK_EQ(f_write(&File, "more", 4, &nWritten), FR_OK);
    CHECK_EQ(nWritten, 0u);

    FatFsHostFailSync(true);
    CHECK_EQ(f_sync(&File), FR_DISK_ERR);

    FatFsHostClearFaults();

    CHECK_EQ(f_sync(&File), FR_OK);
    f_close(&File);

    remove(path.c_str());
}

// f_size() is read straight out of the FIL, so a write that does not maintain it
// leaves an appending caller seeking to a stale end of file.
TEST(fatfs_seam_tracks_the_file_size_across_writes)
{
    const std::string path = TestDataDir() + "/seam-objsize.bin";
    remove(path.c_str());

    FIL File;
    CHECK_EQ(f_open(&File, path.c_str(), FA_CREATE_ALWAYS | FA_WRITE), FR_OK);
    CHECK_EQ((unsigned long long)f_size(&File), (unsigned long long)0);

    UINT nWritten = 0;
    CHECK_EQ(f_write(&File, "0123456789", 10, &nWritten), FR_OK);
    CHECK_EQ(nWritten, 10u);
    CHECK_EQ((unsigned long long)f_size(&File), (unsigned long long)10);
    f_close(&File);

    // Reopening and appending has to carry on from 10, not from 0.
    CHECK_EQ(f_open(&File, path.c_str(), FA_WRITE | FA_OPEN_ALWAYS), FR_OK);
    CHECK_EQ((unsigned long long)f_size(&File), (unsigned long long)10);
    CHECK_EQ(f_lseek(&File, f_size(&File)), FR_OK);
    CHECK_EQ(f_write(&File, "abc", 3, &nWritten), FR_OK);
    CHECK_EQ((unsigned long long)f_size(&File), (unsigned long long)13);
    f_close(&File);

    remove(path.c_str());
}
