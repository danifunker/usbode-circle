#ifndef _ftpserver_fatfspath_h
#define _ftpserver_fatfspath_h

#include <stddef.h>
#include <string.h>

//
// Comparing two FatFs paths for "same file".
//
// This lives in its own header, separate from the FTP worker, because the
// worker needs a socket stack and cannot be compiled into the host test suite -
// and the guard that stops a client deleting, renaming or overwriting the
// mounted image failed TWICE in a row purely on path spelling, with no test
// able to see it:
//
//   "1://Alien.cue"  RealPath() formats "%s/%s" onto m_CurrentPath, which is
//                    "1:/" for the whole session when the worker auto-enters
//                    the images partition on connect.
//   "1:Alien.cue"    FTPPathToFatFsPath() consumes the slash that follows the
//                    volume when it turns an absolute FTP path into a FatFs
//                    one, and never puts it back.
//   "1:/Alien.cue"   what SCSITBService::GetCurrentCDPath() always reports.
//
// FatFs resolves all three to the same file - a drive-relative path is taken
// against that volume's current directory, which is its root here - so every
// spelling works for f_open/f_unlink/f_rename and only a string comparison can
// tell them apart. That is exactly the comparison the guard is.
//

// Canonical form: separators collapsed, no trailing separator, and always a
// separator directly after the volume colon.
static inline void NormalizeFatFsPath(const char *pIn, char *pOut, size_t nOutSize)
{
    size_t nOut = 0;
    bool bVolumeSeen = false;

    // Two spare bytes: one for a separator this may insert after the volume,
    // one for the terminator.
    for (size_t i = 0; pIn[i] != '\0' && nOut + 2 < nOutSize; i++)
    {
        if (pIn[i] == '/' && nOut > 0 && pOut[nOut - 1] == '/')
            continue; // collapse repeated separators

        pOut[nOut++] = pIn[i];

        if (pIn[i] == ':' && !bVolumeSeen)
        {
            bVolumeSeen = true;
            if (pIn[i + 1] != '/' && pIn[i + 1] != '\0')
                pOut[nOut++] = '/'; // "1:name" -> "1:/name"
        }
    }

    while (nOut > 1 && pOut[nOut - 1] == '/')
        nOut--;

    pOut[nOut] = '\0';
}

// Whether two FatFs paths name the same file. Case-insensitive, because FAT is.
static inline bool FatFsPathsEqual(const char *pA, const char *pB, size_t nMax)
{
    if (pA == nullptr || pB == nullptr)
        return false;

    // 520 covers MAX_PATH_LEN (512) plus the inserted separator and terminator.
    char NormA[520];
    char NormB[520];
    if (nMax > sizeof(NormA))
        nMax = sizeof(NormA);

    NormalizeFatFsPath(pA, NormA, nMax);
    NormalizeFatFsPath(pB, NormB, nMax);

    return strcasecmp(NormA, NormB) == 0;
}

#endif
