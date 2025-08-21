#ifndef _HTTPDEVICE_H
#define _HTTPDEVICE_H

#include <circle/device.h>
#include <circle/fs/partitionmanager.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/sysconfig.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>

#include "filetype.h"
#include "cuedevice.h"

class HTTPFileDevice : public ICueDevice {
   public:
    HTTPFileDevice(FIL* pFile, char* cue_str = nullptr);
    ~HTTPFileDevice(void);

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    u64 Tell() const;
    const char* GetCueSheet() const;

   private:
};

#endif
