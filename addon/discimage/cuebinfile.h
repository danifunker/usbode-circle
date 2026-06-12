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
#include <cueparser/cueparser.h>
#include "util.h"
#include "filetype.h"
#include "cuedevice.h"

#define DEFAULT_IMAGE_FILENAME "image.iso"

/// CUE/BIN (single- and multi-file) and ISO image support.
///
/// The device presents a flat virtual byte space that is exactly the
/// concatenation implied by CUEParser's accounting of the normalized cue
/// sheet returned by GetCueSheet(): track data comes from the bin file(s),
/// and regions with no stored data (inter-session gaps, unstored pregaps)
/// read as zeros.
class CCueBinFileDevice : public ICueDevice {
   public:
    /// Multi-file CUE/BIN. cuePath is the full path of the .cue (used to
    /// resolve the referenced bin files); cue_str is the cue sheet text
    /// (copied). Call Init() afterwards; it opens all referenced files.
    CCueBinFileDevice(const char* cuePath, const char* cue_str, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);

    /// Single-file ISO/TOAST image without a cue sheet on disk. Takes
    /// ownership of pFile. Call Init() afterwards.
    CCueBinFileDevice(FIL* pFile, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);

    ~CCueBinFileDevice(void);

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
    FileType GetFileType() const override {
        return m_FileType; // Can be ISO or CUEBIN
    }

    int GetNumTracks() const override;
    u32 GetTrackStart(int track) const override;
    u32 GetTrackLength(int track) const override;
    bool IsAudioTrack(int track) const override;

    // CUE/BIN files do NOT have subchannel data
    bool HasSubchannelData() const override { return false; }
    int ReadSubchannel(u32 lba, u8* subchannel) override { return -1; }

    // From CATALOG / ISRC lines in the cue sheet
    bool GetMCN(char mcn[14]) const override;
    bool GetISRC(int track, char isrc[13]) const override;

    // ========================================================================
    // ICueDevice interface
    // ========================================================================
    const char* GetCueSheet() const override;

   protected:
    static const int MaxFiles = 100;
    static const int MaxSegments = 256;
    static const int MaxTracks = 99;

    /// One contiguous piece of the virtual byte space
    struct Segment {
        u64 vstart;     // virtual byte position
        u64 vlen;       // length in bytes
        int fileIdx;    // index into m_Files, or -1 for a zero-filled gap
        u64 fileOffset; // byte offset within the file where this run begins
    };

    bool ParseAndBuild();
    int OpenTrackFile(const char* filename, u64* pSize); // returns file index or -1
    bool EmitNormalizedCue();
    void ScanCatalogISRC();
    int FindSegment(u64 vpos) const;

    // Map an LBA to the backing file and byte offset within it.
    // Returns false for LBAs in zero-filled gaps or outside any track.
    bool LBAToFileOffset(u32 lba, int* pFileIdx, u64* pFileOffset) const;

    char m_DirPrefix[512] = {0}; // directory of the cue file, incl. trailing '/'
    char* m_SourceCue = nullptr; // original cue text (multi-file ctor only)

    FIL* m_Files[MaxFiles] = {nullptr};
    DWORD* m_CLMTs[MaxFiles] = {nullptr};
    int m_nFiles = 0;

    Segment m_Segments[MaxSegments];
    int m_nSegments = 0;

    CUETrackInfo m_Tracks[MaxTracks]; // LBAs include inter-session gap shifts
    int m_nTracks = 0;
    u32 m_LeadoutLBA = 0;

    char m_MCN[14] = {0};                 // CATALOG line, if any
    char m_ISRC[MaxTracks][13] = {{0}};   // ISRC per track index, if any

    u64 m_vpos = 0;
    u64 m_vsize = 0;
    mutable int m_LastSeg = 0;

    char* m_NormalizedCue = nullptr;
    FileType m_FileType = FileType::ISO;
    MEDIA_TYPE m_mediaType;

    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
};

#endif
