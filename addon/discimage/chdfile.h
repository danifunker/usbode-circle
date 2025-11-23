#ifndef _CHDFILEDEVICE_H
#define _CHDFILEDEVICE_H

#include <circle/device.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <fatfs/ff.h>
#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include "filetype.h"
#include "chdevice.h"

// Internal track info structure
struct CHDTrackInfo {
    u32 trackNumber;
    u32 startLBA;
    u32 frames;
    u32 trackType;  // CD_TRACK_MODE1, CD_TRACK_AUDIO, etc.
    u32 dataSize;   // bytes per frame
};

/// Implementation of CHD image support (MAME compressed hunks format)
class CCHDFileDevice : public ICHDDevice {
   public:
    CCHDFileDevice(const char* chd_filename, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CCHDFileDevice(void);
    bool Init();

    // ========================================================================
    // CDevice interface
    // ========================================================================
    int Read(void* pBuffer, size_t nCount) override;
    int Write(const void* pBuffer, size_t nCount) override;
    
    // ========================================================================
    // IImageDevice interface
    // ========================================================================
    u64 Seek(u64 ullOffset) override;
    u64 GetSize(void) const override;
    u64 Tell() const override;
    MEDIA_TYPE GetMediaType() const override { return m_mediaType; }
    FileType GetFileType() const override { return FileType::CHD; }
    
    // Track information from CHD metadata
    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;
    
    // Subchannel support
    bool HasSubchannelData() const override { return m_hasSubchannels; }
    int ReadSubchannel(u32 lba, u8* subchannel) override;
    
    /// Get a generated CUE sheet for backward compatibility
    const char* GetCueSheet() const override { return m_cue_sheet; }

   private:
    const char* m_chd_filename;
    MEDIA_TYPE m_mediaType;
    chd_file* m_chd;
    bool m_hasSubchannels;
    char* m_cue_sheet;
    
    u64 m_currentOffset;
    u32 m_frameSize;
    
    // Track info parsed from CHD metadata
    CHDTrackInfo m_tracks[CD_MAX_TRACKS];
    int m_numTracks;

    // Hunk cache
    u8* m_hunkBuffer;
    u32 m_hunkSize;
    u32 m_cachedHunkNum;
    int m_lastTrackIndex;
    
    // Helper to parse CHD track metadata
    bool ParseTrackMetadata();
    
    // Helper to generate CUE sheet from CHD metadata
    void GenerateCueSheet();
};

#endif