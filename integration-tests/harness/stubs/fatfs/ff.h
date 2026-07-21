//
// Host-build stub for <fatfs/ff.h>.
//
// This is NOT a reimplementation of FatFs. It is a thin seam that lets the
// REAL disc-image readers (addon/discimage/cuebinfile.cpp, mdsfile.cpp,
// util.cpp) open image files on the build machine. Every f_* entry point
// here is backed by host stdio (fopen/fread/fseek) in fatfs_host.cpp. The
// readers' own logic (cache windows, per-track sector math, LBA translation)
// is compiled and exercised unchanged; only the raw file access is retargeted
// off the Pi's SD card onto the host filesystem.
//
#ifndef _fatfs_ff_h
#define _fatfs_ff_h

#include <stddef.h>
#include <stdint.h>

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   UINT;
// DWORD is also typedef'd (to u32) by the scsitbservice stub that gets pulled
// in transitively; keep this identical (unsigned int == uint32_t on the LP64
// hosts we build on) so the redefinition is allowed rather than a conflict.
typedef unsigned int   DWORD;
typedef uint64_t       QWORD;
typedef uint64_t       FSIZE_t;
typedef uint64_t       LBA_t;
typedef char           TCHAR;

// Subset of the real FRESULT codes actually referenced by the readers.
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

// Object identifier: the readers only ever read obj.objsize (via f_size()).
typedef struct {
    FSIZE_t objsize;
} FFOBJID;

// Directory attribute bits (only AM_DIR is referenced).
#define AM_RDO  0x01
#define AM_HID  0x02
#define AM_SYS  0x04
#define AM_DIR  0x10
#define AM_ARC  0x20

// Directory-entry info. Present so mdsfile.cpp's companion-file scan compiles
// and links; MDS is not exercised by the tests, so f_opendir/f_readdir below
// are not backed by a real host directory walk.
typedef struct {
    FSIZE_t fsize;
    WORD    fdate;
    WORD    ftime;
    BYTE    fattrib;
    TCHAR   fname[256];
} FILINFO;

typedef struct {
    void* host_dir;
} DIR;

// File object. Layout is deliberately minimal: it carries just the fields the
// real readers touch (obj.objsize via f_size, fptr via f_tell, and cltbl for
// the fast-seek optimizer) plus a private host FILE* handle.
typedef struct {
    FFOBJID  obj;      // object.objsize -> file size (f_size)
    FSIZE_t  fptr;     // current file pointer (f_tell)
    DWORD*   cltbl;    // fast-seek cluster link map (set by FatFsOptimizer)
    void*    host_fp;  // backing host FILE* (opaque to firmware code)
} FIL;

// Access-mode flags (values match real FatFs; only FA_READ is used here).
#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_OPEN_EXISTING  0x00
#define FA_CREATE_NEW     0x04
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10
#define FA_OPEN_APPEND    0x30

// Special f_lseek() offset that asks FatFs to build the fast-seek link map.
// On the host there is no FAT, so fatfs_host.cpp treats this as a successful
// no-op and the readers proceed with ordinary seeks.
#define CREATE_LINKMAP  ((FSIZE_t)0 - 1)

#ifdef __cplusplus
extern "C" {
#endif

FRESULT f_open  (FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close (FIL* fp);
FRESULT f_read  (FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek (FIL* fp, FSIZE_t ofs);

// Directory walk: link-only stubs for mdsfile.cpp (MDS is not under test).
FRESULT f_opendir  (DIR* dp, const TCHAR* path);
FRESULT f_readdir  (DIR* dp, FILINFO* fno);
FRESULT f_closedir (DIR* dp);

#ifdef __cplusplus
}
#endif

#define f_tell(fp)  ((fp)->fptr)
#define f_size(fp)  ((fp)->obj.objsize)
#define f_eof(fp)   ((int)((fp)->fptr == (fp)->obj.objsize))
#define f_rewind(fp) f_lseek((fp), 0)

#endif
