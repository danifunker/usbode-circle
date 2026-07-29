#ifndef _ftpserver_fatfspath_h
#define _ftpserver_fatfspath_h

#include <stddef.h>
#include <string.h>

//
// Comparing two FatFs paths for "same file". Separate from the FTP worker so the
// host test suite can reach it without a socket stack.
//
// The same file arrives spelled three ways - "1://Alien.cue" from RealPath(),
// "1:Alien.cue" from FTPPathToFatFsPath(), "1:/Alien.cue" from
// GetCurrentCDPath() - and FatFs opens all three, so only a string comparison
// distinguishes them. That comparison is what the mounted-image guard is.
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
