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
    u64 GetByteOffsetForLBA(u32 lba) const override;
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

    // Read-ahead cache: Seek() only records the logical position (host
    // reads always Seek() then Read(), so the underlying FIL cursor doesn't
    // need to track it). Read() serves from the cache when possible, and on
    // a miss reads a larger window than requested so that the immediately
    // following sequential read (the common case for game/OS data and
    // Redbook streaming) becomes a cache hit instead of another SD card
    // access - this avoids the additional read latency showing up as
    // stutter on the low-bandwidth USB 1.1 link.
    static constexpr size_t CacheSize = 128 * 1024;
    u8* m_pCacheBuffer = nullptr;
    u64 m_nCacheStart = 0;
    size_t m_nCacheLen = 0;
    u64 m_nLogicalPos = 0;
    
    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
};

#endif