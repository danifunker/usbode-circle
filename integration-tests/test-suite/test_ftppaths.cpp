//
// The FTP mounted-image guard is a path comparison, and it has now been wrong
// twice in a row for the same underlying reason: the same file arrives spelled
// differently depending on how the client addressed it, and every spelling
// works for FatFs, so nothing except the comparison itself can notice.
//
// ftpworker.cpp cannot be compiled here (it needs the socket stack), so the
// comparison lives in its own header and is pinned directly.
//
#include "framework.h"

#include <ftpserver/fatfspath.h>

// What SCSITBService::GetCurrentCDPath() always reports.
static const char *kMounted = "1:/Alien Trilogy.cue";

// Every spelling below is a real one produced by the FTP worker, not an
// invented variation:
//
//   "1://x"  RealPath() formats "%s/%s" and m_CurrentPath is "1:/" for the
//            whole session when the worker auto-enters the images partition
//            on connect. This is what a plain "DELE x" produces.
//   "1:x"    FTPPathToFatFsPath() eats the separator after the volume when it
//            converts an absolute FTP path, and never restores it. This is
//            what a client sending "/1/x" produces - and it is what defeated
//            the first fix, which only collapsed duplicate separators.
TEST(ftp_guard_matches_every_spelling_of_the_mounted_image)
{
    CHECK(FatFsPathsEqual("1:/Alien Trilogy.cue", kMounted, 512));
    CHECK(FatFsPathsEqual("1://Alien Trilogy.cue", kMounted, 512));
    CHECK(FatFsPathsEqual("1:Alien Trilogy.cue", kMounted, 512));
    CHECK(FatFsPathsEqual("1:///Alien Trilogy.cue", kMounted, 512));

    // FAT is case-insensitive, so a differently-cased name is the same file
    // and must not slip past the guard.
    CHECK(FatFsPathsEqual("1:/ALIEN TRILOGY.CUE", kMounted, 512));
    CHECK(FatFsPathsEqual("1:alien trilogy.cue", kMounted, 512));
}

// The guard must not over-reach either: refusing to delete files that are not
// mounted would be its own bug, and a prefix match would do exactly that.
TEST(ftp_guard_does_not_match_other_files)
{
    CHECK(!FatFsPathsEqual("1:/Alien Trilogy.bin", kMounted, 512));
    CHECK(!FatFsPathsEqual("1:/Alien Trilogy.cue.bak", kMounted, 512));
    CHECK(!FatFsPathsEqual("1:/Alien", kMounted, 512));
    CHECK(!FatFsPathsEqual("1:/Games/Alien Trilogy.cue", kMounted, 512));

    // Same name, different volume: 0: is the boot partition, not images.
    CHECK(!FatFsPathsEqual("0:/Alien Trilogy.cue", kMounted, 512));

    CHECK(!FatFsPathsEqual(nullptr, kMounted, 512));
    CHECK(!FatFsPathsEqual("1:/x.cue", nullptr, 512));
}

// Subfolders are the common case for anyone who organises their images, and
// the CWD path builds them by a different route than the root case.
TEST(ftp_guard_handles_paths_in_subfolders)
{
    const char *mounted = "1:/Games/RPG/game.cue";

    CHECK(FatFsPathsEqual("1:/Games/RPG/game.cue", mounted, 512));
    CHECK(FatFsPathsEqual("1://Games/RPG/game.cue", mounted, 512));
    CHECK(FatFsPathsEqual("1:Games/RPG/game.cue", mounted, 512));
    CHECK(FatFsPathsEqual("1:/Games//RPG/game.cue", mounted, 512));

    CHECK(!FatFsPathsEqual("1:/Games/RPG2/game.cue", mounted, 512));
    CHECK(!FatFsPathsEqual("1:/Games/game.cue", mounted, 512));
}

// Normalization details worth stating outright, since the guard rests on them.
TEST(fatfs_path_normalization_is_canonical)
{
    char out[520];

    NormalizeFatFsPath("1://a//b///c.cue", out, sizeof(out));
    CHECK(strcmp(out, "1:/a/b/c.cue") == 0);

    NormalizeFatFsPath("1:a/b.cue", out, sizeof(out));
    CHECK(strcmp(out, "1:/a/b.cue") == 0);

    // A trailing separator names the same directory as none.
    NormalizeFatFsPath("1:/Games/", out, sizeof(out));
    CHECK(strcmp(out, "1:/Games") == 0);

    // A bare volume is left alone rather than growing a separator.
    NormalizeFatFsPath("1:", out, sizeof(out));
    CHECK(strcmp(out, "1:") == 0);

    // Truncation must still terminate, never run off the buffer.
    char small[8];
    NormalizeFatFsPath("1:/averylongname.cue", small, sizeof(small));
    CHECK(strlen(small) < sizeof(small));
}
