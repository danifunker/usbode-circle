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

#include <stdio.h>

extern "C" {

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
    if (!fp || !path) {
        return FR_INVALID_PARAMETER;
    }
    // The readers only ever open images read-only.
    FILE* f = fopen(path, "rb");
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
    size_t n = fwrite(buff, 1, btw, (FILE*)fp->host_fp);
    fp->fptr += n;
    if (bw) {
        *bw = (UINT)n;
    }
    return (n == btw) ? FR_OK : FR_DISK_ERR;
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
        return FR_OK;
    }
    if (fseeko((FILE*)fp->host_fp, (off_t)ofs, SEEK_SET) != 0) {
        return FR_DISK_ERR;
    }
    fp->fptr = ofs;
    return FR_OK;
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
