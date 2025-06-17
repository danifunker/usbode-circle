#ifndef _LOOPBACKFILEDEVICE_H
#define _LOOPBACKFILEDEVICE_H

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

#define DEFAULT_IMAGE_FILENAME "image.iso"

class CCueBinFileDevice : public CDevice {
   public:
    CCueBinFileDevice(FIL* pFile, char* cue_str = nullptr);
    ~CCueBinFileDevice(void);

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    const char* GetCueSheet() const;

   private:
    FIL* m_pFile;
    FileType m_FileType = FileType::ISO;
    char* m_cue_str = nullptr;
    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
};

#endif
