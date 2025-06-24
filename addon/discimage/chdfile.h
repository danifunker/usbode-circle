#ifndef _CHDFILEDEVICE_H
#define _CHDFILEDEVICE_H

#include <circle/device.h>
#include <fatfs/ff.h>
#include "filetype.h"
#include "../chdparser/chdparser.h"

class CCHDFileDevice : public CDevice {
public:
    CCHDFileDevice(FIL* pFile);
    ~CCHDFileDevice(void);

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    const char* GetCueSheet() const;

private:
    FIL* m_pFile;
    CHDParser m_chdParser;
    char* m_cueStr;
    u64 m_currentPosition;
    u64 m_totalSize;
};
#endif