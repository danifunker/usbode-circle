//
// discimage_host.cpp
//
// Host backing for the one piece of addon/discimage/util.cpp that the real
// CUE/BIN reader links against: FatFsOptimizer. The optimizer builds a FatFs
// cluster link map so seeks skip FAT-chain walks on the Pi's SD card. There
// is no FAT under the host filesystem, so fast seek simply does not apply
// here: EnableFastSeek reports "disabled" and the reader falls back to
// ordinary f_lseek() (functionally identical, just without the on-Pi
// fragmentation optimization). This lets the tests exercise cuebinfile.cpp
// without pulling in util.cpp's image-format factory (and its MDS chain).
//
#include <discimage/util.h>

boolean FatFsOptimizer::EnableFastSeek(FIL* pFile, DWORD** ppCLMT, size_t clmtSize, const char* logPrefix)
{
    (void)pFile;
    (void)clmtSize;
    (void)logPrefix;
    if (ppCLMT) {
        *ppCLMT = nullptr;
    }
    return false;
}

void FatFsOptimizer::DisableFastSeek(DWORD** ppCLMT)
{
    if (ppCLMT && *ppCLMT) {
        delete[] *ppCLMT;
        *ppCLMT = nullptr;
    }
}
