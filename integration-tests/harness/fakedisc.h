//
// fakedisc.h
//
// In-memory IImageDevice with a synthetic cue sheet, standing in for
// CueBinFileDevice. Physical sector size is uniform across the disc so
// GetByteOffsetForLBA() stays a simple multiply (same as a pure-ISO or
// pure-raw BIN image).
//
// Data sectors carry a deterministic pattern: the first 4 bytes are the
// LBA big-endian, the rest is (lba * 7 + offset) & 0xFF, so tests can
// verify READ payloads end to end.
//
#ifndef _test_host_fakedisc_h
#define _test_host_fakedisc_h

#include <discimage/imagedevice.h>

#include <string.h>

#include <string>
#include <vector>

class CFakeImageDevice : public IImageDevice
{
public:
    CFakeImageDevice(std::string cueSheet, std::vector<u8> image, u32 physSectorSize)
        : m_cue(std::move(cueSheet)), m_image(std::move(image)), m_sectorSize(physSectorSize)
    {
    }

    // CDevice
    int Read(void *pBuffer, size_t nCount) override
    {
        if (m_pos >= m_image.size())
        {
            return 0;
        }
        size_t avail = m_image.size() - (size_t)m_pos;
        if (nCount > avail)
        {
            nCount = avail;
        }
        memcpy(pBuffer, m_image.data() + m_pos, nCount);
        m_pos += nCount;
        return (int)nCount;
    }

    // IImageDevice
    u64 Seek(u64 ullOffset) override
    {
        if (ullOffset > m_image.size())
        {
            return (u64)-1;
        }
        m_pos = ullOffset;
        return ullOffset;
    }

    u64 GetSize(void) const override { return m_image.size(); }
    u64 Tell(void) const override { return m_pos; }

    u64 GetByteOffsetForLBA(u32 lba) const override { return (u64)lba * m_sectorSize; }

    FileType GetFileType(void) const override { return FileType::CUEBIN; }

    int GetNumTracks(void) const override { return m_numTracks; }
    u32 GetTrackStart(int track) const override { return 0; }
    u32 GetTrackLength(int track) const override { return 0; }
    bool IsAudioTrack(int track) const override { return false; }

    const char *GetCueSheet(void) const override { return m_cue.c_str(); }

    int m_numTracks = 1;

private:
    std::string m_cue;
    std::vector<u8> m_image;
    u32 m_sectorSize;
    u64 m_pos = 0;
};

// Fills one sector's worth of bytes with the deterministic test pattern.
void FillPatternSector(u8 *dest, u32 lba, u32 sectorSize);

// Single MODE1/2048 data track ("ISO style"), physical sector size 2048.
CFakeImageDevice *MakeDataISO(u32 numSectors);

// Pure audio CD: nTracks AUDIO tracks of sectorsPerTrack each, 2352-byte
// sectors.
CFakeImageDevice *MakeAudioCD(int nTracks, u32 sectorsPerTrack);

// Mixed mode: one MODE1/2352 data track followed by audio tracks, uniform
// 2352-byte physical sectors.
CFakeImageDevice *MakeMixedModeCD(u32 dataSectors, int nAudioTracks, u32 audioSectorsPerTrack);

#endif
