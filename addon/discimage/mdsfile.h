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
#include "mdsdevice.h"
#include "../mdsparser/mdsparser.h"

/// Implementation of MDS/MDF image support (Alcohol 120% format)
/// MDS format includes subchannel data, making it suitable for copy-protected discs
class CMDSFileDevice : public IMDSDevice {
   public:
    CMDSFileDevice(const char* mds_filename, char* mds_str, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CMDSFileDevice(void);
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
    FileType GetFileType() const override { return FileType::MDS; }
    
    // Track information from MDS parser
    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;
    
    // Subchannel support - the key feature of MDS format
    bool HasSubchannelData() const override { return m_hasSubchannels; }
    int ReadSubchannel(u32 lba, u8* subchannel) override;
    
    // ========================================================================
    // IMDSDevice interface
    // ========================================================================
    MDSParser* GetParser() const override { return m_parser; }
    
    /// Get a generated CUE sheet for backward compatibility
    const char* GetCueSheet() const override { return m_cue_sheet; }

   private:
    /// Mapping of one track between the virtual byte space implied by the
    /// generated cue sheet and the physical MDF layout
    struct TrackMap {
        u64 vstart;       // virtual byte offset of the track's INDEX 01
        u32 data_start;   // LBA of INDEX 01 (MDS start_sector, disc-absolute)
        u32 length;       // track length in frames (0 = unknown)
        u32 base_size;    // logical bytes per sector (sector_size minus subchannel)
        u32 sector_size;  // physical bytes per sector in the MDF
        u64 start_offset; // physical byte offset of the track data in the MDF
        u8 subchannel;    // 0 = none, 0x08 = 96-byte interleaved P-W appended
        u8 session;
        u8 track_number;  // point
        bool audio;
        const char* cue_type; // cue TRACK type string (static storage)
    };
    static const int MaxTracks = 99;

    bool BuildTrackMap();
    bool GenerateCueSheet();
    int FindMapForV(u64 vpos) const;   // track whose virtual range contains vpos
    int FindMapForLBA(u32 lba) const;

    FIL* m_pFile;
    char* m_mds_str = nullptr;
    char* m_cue_sheet = nullptr;  // Normalized cue consumed by the gadget
    const char* m_mds_filename;
    MEDIA_TYPE m_mediaType;
    MDSParser* m_parser;
    DWORD* m_pCLMT;
    bool m_hasSubchannels = false;

    TrackMap m_Map[MaxTracks];
    int m_nMapCount = 0;
    u64 m_vpos = 0;
    u64 m_vsize = 0;
};

#endif