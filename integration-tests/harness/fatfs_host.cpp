//
// fatfs_host.cpp
//
// Host backend for the FatFs seam declared in stubs/fatfs/ff.h. Maps the
// handful of f_* calls the real disc-image readers make onto plain host
// stdio, so cuebinfile/mdsfile/util.cpp can open real image files off the
// build machine's filesystem. No FatFs or reader logic is reimplemented;
// this is purely the raw-file access boundary.
//
#include <fatfs/ff.h>

#include "fatfs_host.h"

#include <stdio.h>

#include <string>

namespace {

constexpr size_t kNoWriteLimit = (size_t)-1;

size_t s_WriteLimit = kNoWriteLimit;
size_t s_BytesAccepted = 0;
bool s_SyncFails = false;
unsigned s_LinkmapCount = 0;
unsigned s_FailRenameAt = 0;
unsigned s_FailRenameCount = 0;
unsigned s_RenameCount = 0;
std::string s_DriveRoot;

// "0:/wpa_supplicant.conf" -> "<root>/wpa_supplicant.conf". Paths without a
// drive prefix, and every path at all while no root is set, pass through.
const char* MapPath(const char* path, std::string& storage)
{
    if (s_DriveRoot.empty() || path == nullptr) {
        return path;
    }
    if (!(path[0] >= '0' && path[0] <= '9') || path[1] != ':') {
        return path;
    }
    storage = s_DriveRoot;
    if (path[2] != '/') {
        storage += '/';
    }
    storage += path + 2;
    return storage.c_str();
}

}  // namespace

void FatFsHostSetWriteLimit(size_t nBytes)
{
    s_WriteLimit = nBytes;
    s_BytesAccepted = 0;
}

void FatFsHostFailSync(bool bFail)
{
    s_SyncFails = bFail;
}

void FatFsHostFailRename(unsigned nFirst, unsigned nCount)
{
    s_FailRenameAt = nFirst;
    s_FailRenameCount = nCount;
    s_RenameCount = 0;
}

void FatFsHostSetDriveRoot(const char* pPath)
{
    s_DriveRoot = (pPath != nullptr) ? pPath : "";
}

void FatFsHostClearFaults(void)
{
    s_WriteLimit = kNoWriteLimit;
    s_BytesAccepted = 0;
    s_SyncFails = false;
    s_FailRenameAt = 0;
    s_FailRenameCount = 0;
    s_RenameCount = 0;
}

void FatFsHostResetLinkmapCount(void)
{
    s_LinkmapCount = 0;
}

unsigned FatFsHostLinkmapCount(void)
{
    return s_LinkmapCount;
}

extern "C" {

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
    if (!fp || !path) {
        return FR_INVALID_PARAMETER;
    }

    std::string mapped;
    path = MapPath(path, mapped);

    // FA_OPEN_ALWAYS is "r+b" falling back to "w+b" only when the file is
    // missing, which keeps a bad directory an error.
    const char* stdioMode = "rb";
    if (mode & (FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS)) {
        if (mode & FA_CREATE_ALWAYS) {
            stdioMode = "w+b";
        } else if ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) {
            stdioMode = "a+b";
        } else {
            stdioMode = "r+b";
        }
    }

    // stdio has no "create only if absent" mode, and "r+b" would open the very
    // file the caller asked to be protected from.
    if (mode & FA_CREATE_NEW) {
        FILE* existing = fopen(path, "rb");
        if (existing) {
            fclose(existing);
            return FR_EXIST;
        }
    }

    FILE* f = fopen(path, stdioMode);
    if (!f && (mode & (FA_OPEN_ALWAYS | FA_CREATE_NEW))) {
        f = fopen(path, "w+b");
    }
    if (!f) {
        return FR_NO_FILE;
    }
    if (fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        return FR_DISK_ERR;
    }
    off_t size = ftello(f);
    if (size < 0) {
        fclose(f);
        return FR_DISK_ERR;
    }
    rewind(f);

    fp->obj.objsize = (FSIZE_t)size;
    fp->fptr = 0;
    fp->cltbl = nullptr;
    fp->host_fp = f;
    return FR_OK;
}

FRESULT f_close(FIL* fp)
{
    if (!fp || !fp->host_fp) {
        return FR_INVALID_OBJECT;
    }
    fclose((FILE*)fp->host_fp);
    fp->host_fp = nullptr;
    return FR_OK;
}

FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br)
{
    if (br) {
        *br = 0;
    }
    if (!fp || !fp->host_fp || !buff) {
        return FR_INVALID_OBJECT;
    }
    size_t n = fread(buff, 1, btr, (FILE*)fp->host_fp);
    if (n != btr && ferror((FILE*)fp->host_fp)) {
        return FR_DISK_ERR;
    }
    fp->fptr += n;
    if (br) {
        *br = (UINT)n;
    }
    return FR_OK;
}

FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw)
{
    if (bw) {
        *bw = 0;
    }
    if (!fp || !fp->host_fp || !buff) {
        return FR_INVALID_OBJECT;
    }
    UINT nAccept = btw;
    if (s_WriteLimit != kNoWriteLimit) {
        const size_t room = (s_WriteLimit > s_BytesAccepted) ? s_WriteLimit - s_BytesAccepted : 0;
        if (room < nAccept) {
            nAccept = (UINT)room;
        }
    }

    size_t n = nAccept > 0 ? fwrite(buff, 1, nAccept, (FILE*)fp->host_fp) : 0;
    s_BytesAccepted += n;
    fp->fptr += n;
    if (fp->fptr > fp->obj.objsize) {
        fp->obj.objsize = fp->fptr;
    }
    if (bw) {
        *bw = (UINT)n;
    }
    // A full card is FR_OK with a short count. Only the injected cap gets that;
    // past it a short write is a real host I/O error.
    if (nAccept < btw) {
        return FR_OK;
    }
    return (n == btw) ? FR_OK : FR_DISK_ERR;
}

FRESULT f_sync(FIL* fp)
{
    if (s_SyncFails) {
        return FR_DISK_ERR;
    }
    if (!fp || !fp->host_fp) {
        return FR_INVALID_OBJECT;
    }
    return fflush((FILE*)fp->host_fp) == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_lseek(FIL* fp, FSIZE_t ofs)
{
    if (!fp || !fp->host_fp) {
        return FR_INVALID_OBJECT;
    }
    // Fast-seek link-map creation: no FAT here, so report success and let the
    // readers fall through to ordinary seeks (functionally identical, just
    // without the on-Pi fragmentation optimization).
    if (ofs == CREATE_LINKMAP) {
        s_LinkmapCount++;
        return FR_OK;
    }
    if (fseeko((FILE*)fp->host_fp, (off_t)ofs, SEEK_SET) != 0) {
        return FR_DISK_ERR;
    }
    fp->fptr = ofs;
    return FR_OK;
}

FRESULT f_unlink(const TCHAR* path)
{
    if (!path) {
        return FR_INVALID_NAME;
    }
    std::string mapped;
    return remove(MapPath(path, mapped)) == 0 ? FR_OK : FR_NO_FILE;
}

FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new)
{
    if (!path_old || !path_new) {
        return FR_INVALID_NAME;
    }
    ++s_RenameCount;
    if (s_FailRenameAt != 0 && s_RenameCount >= s_FailRenameAt &&
        s_RenameCount < s_FailRenameAt + s_FailRenameCount) {
        return FR_DENIED;
    }

    std::string mappedOld;
    std::string mappedNew;
    const char* from = MapPath(path_old, mappedOld);
    const char* to = MapPath(path_new, mappedNew);

    FILE* source = fopen(from, "rb");
    if (!source) {
        return FR_NO_FILE;
    }
    fclose(source);

    // FatFs refuses to clobber an existing destination; rename(2) replaces it.
    FILE* existing = fopen(to, "rb");
    if (existing) {
        fclose(existing);
        return FR_EXIST;
    }

    return rename(from, to) == 0 ? FR_OK : FR_DENIED;
}

// Directory walk: intentionally unbacked. Only mdsfile.cpp calls these, and
// MDS images are not exercised by the tests; these exist so the loader links.
// f_opendir reports "no path" so any accidental MDS load fails cleanly rather
// than silently pretending a directory is empty.
FRESULT f_opendir(DIR* dp, const TCHAR* path)
{
    (void)path;
    if (dp) {
        dp->host_dir = nullptr;
    }
    return FR_NO_PATH;
}

FRESULT f_readdir(DIR* dp, FILINFO* fno)
{
    (void)dp;
    if (fno) {
        fno->fname[0] = '\0';
    }
    return FR_NO_PATH;
}

FRESULT f_closedir(DIR* dp)
{
    (void)dp;
    return FR_OK;
}

} // extern "C"
