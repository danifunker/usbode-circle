#ifndef _CHDFILEDEVICE_H
#define _CHDFILEDEVICE_H

#include <circle/device.h>
#include <circle/fs/partitionmanager.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/sysconfig.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>

#include "imagedevice.h"
#include "filetype.h"

extern "C" {
#include <libchdr/chd.h>
}

typedef struct {
    u32 track_type;
    u32 track_subtype;
    u32 track_frames;
    u32 pregap;
} chd_track_metadata;

class CChdFileDevice : public IImageDevice {
public:
    CChdFileDevice(const char* chd_filename);
    ~CChdFileDevice(void);
    bool Init();

    // CDevice interface
    int Read(void* pBuffer, size_t nCount) override;
    int Write(const void* pBuffer, size_t nCount) override;

    // IImageDevice interface
    u64 Seek(u64 ullOffset) override;
    u64 GetSize(void) const override;
    u64 Tell() const override;
    FileType GetFileType() const override { return FileType::CHD; }

    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;

    const char* GetCueSheet() const override;

private:
    void GetTrackMetadata(int track, chd_track_metadata* metadata) const;

    FIL* m_pFile;
    chd_file* m_chd;
    char* m_cue_sheet;
    const char* m_chd_filename;
    u64 m_ullOffset;
    chd_track_metadata* m_track_metadata;
};

#endif
