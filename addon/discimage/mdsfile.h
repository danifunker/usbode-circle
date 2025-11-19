#ifndef _MDSFILE_H
#define _MDSFILE_H

#include "imagedevice.h"
#include <fatfs/ff.h>

class IMdsDevice : public IImageDevice {
public:
    IMdsDevice() = default;
    virtual ~IMdsDevice() = default;
};

class CMdsFileDevice : public IMdsDevice {
public:
    CMdsFileDevice(const char* mdsPath);
    ~CMdsFileDevice();

    int Read(void* pBuffer, size_t nSize) override;
    int Write(const void* pBuffer, size_t nSize) override;
    u64 Seek(u64 ullOffset) override;
    u64 GetSize(void) const override;
    u64 Tell() const override;

    MEDIA_TYPE GetMediaType() const override;
    const char* GetCueSheet() const override;

private:
    char* m_cueSheet;
    FIL m_mdfFile;
    bool m_fileOpen;
};

#endif // _MDSFILE_H
