#ifndef _CCDFILEDEVICE_H
#define _CCDFILEDEVICE_H

#include "imagedevice.h"
#include <fatfs/ff.h>

class CCcdFileDevice : public IImageDevice {
public:
    CCcdFileDevice(const char* ccd_filename);
    ~CCcdFileDevice(void);

    bool Init();

    // CDevice interface
    int Read(void* pBuffer, size_t nSize) override;
    int Write(const void* pBuffer, size_t nSize) override;

    // IImageDevice interface
    u64 Seek(u64 ullOffset) override;
    u64 GetSize(void) const override;
    u64 Tell() const override;
    FileType GetFileType() const override { return FileType::CCD; }

    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;

    const char* GetCueSheet() const override;

    // Subchannel support
    bool HasSubchannelData() const override;
    int ReadSubchannel(u32 lba, u8* subchannel) override;

private:
    bool ParseCcdFile(const char* ccd_path);
    void GenerateCueSheet();

    char m_ccd_filename[256];
    FIL* m_imgFile;
    FIL* m_subFile;
    char* m_cueSheet;
    bool m_hasSubchannels;

    struct TrackInfo {
        u32 start_lba;
        u32 length;
        bool is_audio;
    };
    TrackInfo* m_tracks;
    int m_numTracks;
};

#endif
