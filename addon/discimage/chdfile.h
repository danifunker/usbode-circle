#ifndef _CHDFILEDEVICE_H
#define _CHDFILEDEVICE_H

#include <circle/device.h>
#include <fatfs/ff.h>
#include "filetype.h"
#include "../chdparser/chdparser.h"
#include "cuebinfile.h"

class CCHDFileDevice : public CCueBinFileDevice {
public:
    CCHDFileDevice(FIL *pFile);
    ~CCHDFileDevice(void);

    // Override base class methods as needed
    int Read(void* pBuffer, size_t nCount) override;
    u64 Seek(u64 ullOffset) override;

private:
    CHDParser m_chdParser;
};

#endif