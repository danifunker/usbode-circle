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
#include "util.h"
#include "filetype.h"
#include "cuedevice.h"  // Now extends IImageDevice

#define DEFAULT_IMAGE_FILENAME "image.iso"

/// Implementation of CUE/BIN and ISO image support
class CCueBinFileDevice : public ICueDevice {
   public:
    CCueBinFileDevice(FIL* pFile, char* cue_str = nullptr, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CCueBinFileDevice(void);

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
    FileType GetFileType() const override { 
        return m_FileType; // Can be ISO or CUEBIN
    }
    
    // Track information - will need to parse CUE sheet
    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;
    
    // CUE/BIN files do NOT have subchannel data
    bool HasSubchannelData() const override { return false; }
    int ReadSubchannel(u32 lba, u8* subchannel) override { return -1; }
    
    // ========================================================================
    // ICueDevice interface
    // ========================================================================
    const char* GetCueSheet() const override; 
    
   private:
    FIL* m_pFile;
    FileType m_FileType = FileType::ISO;
    char* m_cue_str = nullptr;
    MEDIA_TYPE m_mediaType;
    DWORD *m_pCLMT;                       // NEW: Cluster link map table for fast seek
    // Track parsing state (lazy initialization)
    mutable bool m_tracksParsed = false;
    mutable int m_numTracks = 0;
    // TODO: Add track structure storage
    
    void ParseCueSheet() const;
    
    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
};

#endif