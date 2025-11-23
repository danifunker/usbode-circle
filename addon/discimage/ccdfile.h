// addon/discimage/ccdfile.h

#ifndef _CCDFILE_H
#define _CCDFILE_H

#include "imagedevice.h"
#include <fatfs/ff.h>
#include <vector>

struct CcdEntry {
    int Session;
    int Point;
    int ADR;
    int Control;
    int TrackNo;
    int AMin;
    int ASec;
    int AFrame;
    int ALBA;
    int Zero;
    int PMin;
    int PSec;
    int PFrame;
    int PLBA;

    // Optional [Track X] info if available
    // We might not need Track section parsing if Entry is sufficient.
    // libmirage uses Entry to build tracks.
    int Mode;
    int Index0;
    int Index1;
};

struct TrackInfo {
    int number;
    int start_lba;
    int length;
    bool is_audio;
    int pregap;
};

class CCcdFileDevice : public IImageDevice {
public:
    CCcdFileDevice(const char* imgPath, const char* subPath, const char* ccdContent, MEDIA_TYPE mediaType);
    virtual ~CCcdFileDevice();

    bool Init();

    // CDevice implementation
    virtual int Read(void* pBuffer, size_t nCount) override;
    virtual int Write(const void* pBuffer, size_t nCount) override;

    // IImageDevice implementation
    virtual u64 Seek(u64 ullOffset) override;
    virtual u64 GetSize(void) const override;
    virtual u64 Tell() const override;
    virtual FileType GetFileType() const override { return FileType::CCD; }

    virtual int GetNumTracks() const override;
    virtual u32 GetTrackStart(int track) const override;
    virtual u32 GetTrackLength(int track) const override;
    virtual bool IsAudioTrack(int track) const override;

    virtual bool HasSubchannelData() const override { return m_SubFile != nullptr; }
    virtual int ReadSubchannel(u32 lba, u8* subchannel) override;

    virtual const char* GetCueSheet() const override { return m_CueSheet; }

private:
    bool ParseCcd();
    void GenerateCueSheet();

    FIL* m_ImgFile;
    FIL* m_SubFile;
    char* m_ImgPath;
    char* m_SubPath;
    const char* m_CcdContent;
    char* m_CueSheet;

    std::vector<CcdEntry> m_Entries;
    std::vector<TrackInfo> m_Tracks;

    u64 m_CurrentOffset;
    u64 m_MainDataOffset;
    u64 m_SubDataOffset;
    MEDIA_TYPE m_MediaType;
};

#endif
