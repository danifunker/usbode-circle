#ifndef _CUEBINDEVICE_H
#define _CUEBINDEVICE_H

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

#define DEFAULT_IMAGE_FILENAME "image.iso"

class CCueBinFileDevice : public ICueDevice {
   public:
    CCueBinFileDevice(FIL* pFile, char* cue_str = nullptr, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CCueBinFileDevice(void);

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    u64 Tell() const;
    const char* GetCueSheet() const override;
    MEDIA_TYPE GetMediaType() const override;

   private:
    FIL* m_pFile;
    FileType m_FileType = FileType::ISO;
    char* m_cue_str = nullptr;
    MEDIA_TYPE m_mediaType;
    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
};

#endif
