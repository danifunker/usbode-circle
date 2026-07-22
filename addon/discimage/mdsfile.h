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
    /// \param mds_str   The .mds file contents. Takes ownership; freed with delete[].
    /// \param mds_size  Length of mds_str in bytes, so the parser can range
    ///                  check the file offsets it is about to follow.
    CMDSFileDevice(const char* mds_filename, char* mds_str, size_t mds_size,
                   MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
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
    // Init() can fail before any of these are set - an invalid .mds, or a
    // missing MDF - and the caller then destroys the device, so every member
    // the destructor touches has to start out safe to free.
    FIL* m_pFile = nullptr;
    char* m_mds_str = nullptr;
    char* m_cue_sheet = nullptr;  // Generated for compatibility
    /// Copied, not aliased: the loader in util.cpp builds the path in a local
    /// buffer that is gone by the time it returns the device. Sized to match
    /// that buffer.
    char m_mds_filename[512] = {0};
    size_t m_mds_size = 0;
    MEDIA_TYPE m_mediaType = MEDIA_TYPE::CD;
    MDSParser* m_parser = nullptr;
    DWORD* m_pCLMT = nullptr;
    bool m_hasSubchannels = false;

    /// Total frames on the disc, from the track table. The MDF is not a
    /// reliable substitute: with subchannels every sector occupies 2448
    /// bytes, so its length divided by 2352 over-reports the disc.
    u32 m_nTotalFrames = 0;

    /// The LBA Seek() last resolved. Read() needs it because Tell() reports
    /// a PHYSICAL offset in the MDF, which on a 2448-byte-per-sector track
    /// is not lba * 2352.
    u32 m_nCurrentLBA = 0;

    // Helper to find track containing an LBA
    MDS_TrackBlock* FindTrackForLBA(u32 lba, int* sessionOut, int* trackOut) const;

    /// True if any of nSectors frames from firstLBA is on the disc but absent
    /// from the MDF, i.e. inside a pregap the imaging tool did not store.
    bool TouchesUnstoredGap(u32 firstLBA, size_t nSectors) const;

    /// Read that walks frame by frame, serving zeros for unstored ones and
    /// seeking explicitly for the rest. Only worth using when the transfer
    /// actually crosses a gap; Read() keeps its single-f_read path otherwise.
    int ReadAcrossGaps(void* pBuffer, size_t nSize);
};

#endif