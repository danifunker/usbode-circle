#ifndef _MDSFILEDEVICE_H
#define _MDSFILEDEVICE_H

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
#include "../mdsparser/mdsparser.h"

class CMDSFileDevice : public ICueDevice {
   public:
    CMDSFileDevice(const char* mds_filename, char* mds_str, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CMDSFileDevice(void);
    bool Init();

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    u64 Tell() const;
    const char* GetCueSheet() const;
    MEDIA_TYPE GetMediaType() const override { return m_mediaType; }

   private:
    FIL* m_pFile;
    FileType m_FileType = FileType::MDS;
    char* m_mds_str = nullptr;
    char* m_cue_sheet = nullptr;
    const char* m_mds_filename;
    MEDIA_TYPE m_mediaType;
    MDSParser* m_parser;
};

#endif
