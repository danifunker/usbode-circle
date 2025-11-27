#include "chdfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdio.h>

LOGMODULE("chdfile");

CCHDFileDevice::CCHDFileDevice(const char *chd_filename, MEDIA_TYPE mediaType)
    : m_chd_filename(chd_filename),
      m_mediaType(mediaType),
      m_hunkBuffer(nullptr),
      m_hunkSize(0),
      m_cachedHunkNum(UINT32_MAX), 
      m_lastTrackIndex(0)
{
    LOGNOTE("CCHDFileDevice created for: %s", chd_filename);
    memset(m_tracks, 0, sizeof(m_tracks));
}

CCHDFileDevice::~CCHDFileDevice()
{
    if (m_chd)
    {
        chd_close(m_chd);
        m_chd = nullptr;
    }
    if (m_cue_sheet)
    {
        delete[] m_cue_sheet;
        m_cue_sheet = nullptr;
    }
    if (m_hunkBuffer)
    {
        delete[] m_hunkBuffer;
        m_hunkBuffer = nullptr;
    }
}

// -------------------------------------------------------------------------
// PARSER: Uses the Robust 'strstr' logic 
// -------------------------------------------------------------------------
bool CCHDFileDevice::ParseTrackMetadata()
{
    char metadata[256];
    uint32_t metadata_length = sizeof(metadata);
    uint32_t metadata_index = 0;

    m_numTracks = 0;

    while (m_numTracks < CD_MAX_TRACKS)
    {
        memset(metadata, 0, sizeof(metadata));

        chd_error err = chd_get_metadata(m_chd, CDROM_TRACK_METADATA2_TAG,
                                         metadata_index, metadata, metadata_length,
                                         nullptr, nullptr, nullptr);

        if (err != CHDERR_NONE)
        {
            err = chd_get_metadata(m_chd, CDROM_TRACK_METADATA_TAG,
                                   metadata_index, metadata, metadata_length,
                                   nullptr, nullptr, nullptr);
            if (err != CHDERR_NONE)
                break; 
        }

        metadata[sizeof(metadata) - 1] = '\0';

        CHDTrackInfo &track = m_tracks[m_numTracks];
        track.trackNumber = 0;
        track.frames = 0;

        char *p = strstr(metadata, "TRACK:");
        if (p) sscanf(p + 6, "%u", &track.trackNumber);

        p = strstr(metadata, "FRAMES:");
        if (p) sscanf(p + 7, "%u", &track.frames);

        char typeStr[32] = {0};
        p = strstr(metadata, "TYPE:");
        if (p) sscanf(p + 5, "%31s", typeStr);

        if (track.trackNumber > 0 && track.frames > 0)
        {
            track.startLBA = (m_numTracks > 0) ? (m_tracks[m_numTracks - 1].startLBA + m_tracks[m_numTracks - 1].frames) : 0;

            if (strstr(typeStr, "AUDIO"))
            {
                track.trackType = CD_TRACK_AUDIO;
                track.dataSize = 2352;
            }
            else if (strstr(typeStr, "RAW") || strstr(typeStr, "2352"))
            {
                if (strstr(typeStr, "MODE2")) track.trackType = CD_TRACK_MODE2_RAW;
                else track.trackType = CD_TRACK_MODE1_RAW;
                track.dataSize = 2352;
            }
            else if (strstr(typeStr, "MODE1"))
            {
                track.trackType = CD_TRACK_MODE1;
                track.dataSize = 2048;
            }
            else if (strstr(typeStr, "MODE2"))
            {
                 if (strstr(typeStr, "FORM1")) { track.trackType = CD_TRACK_MODE2_FORM1; track.dataSize = 2048; }
                 else if (strstr(typeStr, "FORM2")) { track.trackType = CD_TRACK_MODE2_FORM2; track.dataSize = 2324; }
                 else { track.trackType = CD_TRACK_MODE2; track.dataSize = 2336; }
            }
            else
            {
                track.trackType = CD_TRACK_MODE1_RAW;
                track.dataSize = 2352;
            }

            LOGNOTE("Track %d Parsed: Type=%s, Frames=%u, StartLBA=%u", 
                    track.trackNumber, typeStr, track.frames, track.startLBA);
            m_numTracks++;
        }
        
        metadata_index++;
    }

    return m_numTracks > 0;
}

bool CCHDFileDevice::Init()
{
    LOGNOTE("Initializing CHD file: %s", m_chd_filename);

    chd_error err = chd_open(m_chd_filename, CHD_OPEN_READ, nullptr, &m_chd);
    if (err != CHDERR_NONE)
    {
        LOGERR("Failed to open CHD file: %s (error: %d)", m_chd_filename, err);
        return false;
    }

    const chd_header *header = chd_get_header(m_chd);
    if (!header)
    {
        LOGERR("Failed to get CHD header");
        chd_close(m_chd);
        m_chd = nullptr;
        return false;
    }

    LOGNOTE("CHD version: %d, hunk size: %d bytes", header->version, header->hunkbytes);

    m_hunkSize = header->hunkbytes;
    m_hunkBuffer = new u8[m_hunkSize];
    if (!m_hunkBuffer)
    {
        LOGERR("Failed to allocate hunk buffer");
        chd_close(m_chd);
        m_chd = nullptr;
        return false;
    }

    if (!ParseTrackMetadata())
    {
        LOGERR("No CD-ROM tracks found in CHD");
        chd_close(m_chd);
        m_chd = nullptr;
        return false;
    }

    LOGNOTE("CHD has %d tracks", m_numTracks);
    m_frameSize = m_tracks[0].dataSize;

    bool hasPhysicalSubchannels = (header->unitbytes == CD_FRAME_SIZE);
    bool forceEnableSubchannels = (strstr(m_chd_filename, ".subchan.") != nullptr);

    if (hasPhysicalSubchannels)
    {
        if (forceEnableSubchannels) {
            LOGNOTE("CHD contains subchannel data - ENABLED");
            m_hasSubchannels = true;
        } else {
            LOGNOTE("CHD contains subchannel data - Disabling for compatibility");
            m_hasSubchannels = false;
        }
    }
    else
    {
        m_hasSubchannels = false;
    }

    GenerateCueSheet();
    return true;
}

void CCHDFileDevice::GenerateCueSheet()
{
    size_t bufSize = 4096;
    m_cue_sheet = new char[bufSize];
    char *buf = m_cue_sheet;
    size_t remaining = bufSize;

    int written = snprintf(buf, remaining, "FILE \"%s\" BINARY\n", m_chd_filename);
    buf += written;
    remaining -= written;

    for (int i = 0; i < m_numTracks && remaining > 100; i++)
    {
        const CHDTrackInfo &track = m_tracks[i];
        
        // Accurate Mode Reporting
        const char *mode = "MODE1/2352"; // Default
        if (track.trackType == CD_TRACK_AUDIO) mode = "AUDIO";
        else if (track.dataSize == 2048) mode = "MODE1/2048";
        
        // FIXED: Removed the '+ 150' offset.
        // We calculate MSF relative to the file start (00:00:00)
        u32 lba = track.startLBA;
        
        u32 minutes = lba / (60 * 75);
        u32 seconds = (lba / 75) % 60;
        u32 frames = lba % 75;

        written = snprintf(buf, remaining, "  TRACK %02d %s\n", track.trackNumber, mode);
        buf += written; remaining -= written;

        // FIXED: Mixed Mode Logic
        // If this is Track 2 (Audio) and Track 1 was Data, insert the standard 2-second pregap.
        // This helps Mac OS separate the partitions without shifting the actual file data.
        if (i == 1 && track.trackType == CD_TRACK_AUDIO && m_tracks[0].trackType != CD_TRACK_AUDIO) {
             written = snprintf(buf, remaining, "    PREGAP 00:02:00\n");
             buf += written; remaining -= written;
        }

        written = snprintf(buf, remaining, "    INDEX 01 %02d:%02d:%02d\n",
                           minutes, seconds, frames);
        buf += written; remaining -= written;
    }
    LOGNOTE("Generated CUE sheet");
}

int CCHDFileDevice::Read(void *pBuffer, size_t nCount)
{
    if (!m_chd || !pBuffer) return -1;

    const chd_header *header = chd_get_header(m_chd);
    if (!header) return -1;

    u32 unitBytes = header->unitbytes;
    u32 sectorBytes = CD_MAX_SECTOR_DATA; 
    u32 framesPerHunk = header->hunkbytes / unitBytes;

    size_t bytesRead = 0;
    u8 *dest = static_cast<u8 *>(pBuffer);

    while (bytesRead < nCount)
    {
        u64 absoluteFrame = m_currentOffset / sectorBytes;
        u32 offsetInSector = m_currentOffset % sectorBytes;

        u32 hunkNum = absoluteFrame / framesPerHunk;
        u32 frameInHunk = absoluteFrame % framesPerHunk;

        // ANTI-HANG CHECK
        if (hunkNum >= header->totalhunks) break; 

        if (hunkNum != m_cachedHunkNum)
        {
            chd_error err = chd_read(m_chd, hunkNum, m_hunkBuffer);
            if (err != CHDERR_NONE)
            {
                LOGERR("CHD read error at hunk %u: %d", hunkNum, err);
                return bytesRead > 0 ? bytesRead : -1;
            }
            m_cachedHunkNum = hunkNum;
        }

        u32 frameStartInHunk = frameInHunk * unitBytes;
        u32 readPosition = frameStartInHunk + offsetInSector;

        u32 bytesLeftInSector = sectorBytes - offsetInSector;
        u32 bytesToCopy = nCount - bytesRead;
        if (bytesToCopy > bytesLeftInSector)
            bytesToCopy = bytesLeftInSector;

        memcpy(dest + bytesRead, m_hunkBuffer + readPosition, bytesToCopy);

        // Byte Swap Logic (Audio Only)
        u64 currentLBA = m_currentOffset / sectorBytes;
        u64 lastTrackEnd = m_tracks[m_lastTrackIndex].startLBA + m_tracks[m_lastTrackIndex].frames;
        
        if (currentLBA < m_tracks[m_lastTrackIndex].startLBA || currentLBA >= lastTrackEnd)
        {
            m_lastTrackIndex = -1; 
            for (int i = 0; i < m_numTracks; i++)
            {
                u64 trackEnd = m_tracks[i].startLBA + m_tracks[i].frames;
                if (currentLBA >= m_tracks[i].startLBA && currentLBA < trackEnd)
                {
                    m_lastTrackIndex = i;
                    break;
                }
            }
        }

        if (m_lastTrackIndex >= 0 && m_tracks[m_lastTrackIndex].trackType == CD_TRACK_AUDIO)
        {
            for (u32 i = 0; i < bytesToCopy; i += 2)
            {
                if (bytesRead + i + 1 < nCount)
                {
                    u8 temp = dest[bytesRead + i];
                    dest[bytesRead + i] = dest[bytesRead + i + 1];
                    dest[bytesRead + i + 1] = temp;
                }
            }
        }

        bytesRead += bytesToCopy;
        m_currentOffset += bytesToCopy;
    }

    return bytesRead;
}

int CCHDFileDevice::Write(const void *pBuffer, size_t nCount)
{
    return -1;
}

u64 CCHDFileDevice::Seek(u64 ullOffset)
{
    m_currentOffset = ullOffset;
    return m_currentOffset;
}

u64 CCHDFileDevice::GetSize() const {
    if (!m_chd) return 0;
    const chd_header* header = chd_get_header(m_chd);
    if (!header) return 0;
    
    u64 totalFrames = header->logicalbytes / header->unitbytes;
    return totalFrames * CD_MAX_SECTOR_DATA; 
}

u64 CCHDFileDevice::Tell() const
{
    return m_currentOffset;
}

int CCHDFileDevice::GetNumTracks() const
{
    return m_numTracks;
}

u32 CCHDFileDevice::GetTrackStart(int track) const
{
    if (track < 1 || track > m_numTracks) return 0;
    return m_tracks[track - 1].startLBA;
}

u32 CCHDFileDevice::GetTrackLength(int track) const
{
    if (track < 1 || track > m_numTracks) return 0;
    return m_tracks[track - 1].frames;
}

bool CCHDFileDevice::IsAudioTrack(int track) const
{
    if (track < 1 || track > m_numTracks) return false;
    return m_tracks[track - 1].trackType == CD_TRACK_AUDIO;
}

int CCHDFileDevice::ReadSubchannel(u32 lba, u8 *subchannel)
{
    if (!m_hasSubchannels || !subchannel) return -1;

    const chd_header *header = chd_get_header(m_chd);
    u32 framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;
    u32 hunkNum = lba / framesPerHunk;
    
    if (hunkNum >= header->totalhunks) return -1;

    if (hunkNum != m_cachedHunkNum)
    {
        chd_error err = chd_read(m_chd, hunkNum, m_hunkBuffer);
        if (err != CHDERR_NONE) return -1;
        m_cachedHunkNum = hunkNum;
    }

    u32 frameInHunk = lba % framesPerHunk;
    u32 frameOffset = frameInHunk * CD_FRAME_SIZE;
    memcpy(subchannel, m_hunkBuffer + frameOffset + CD_MAX_SECTOR_DATA, CD_MAX_SUBCODE_DATA);

    return CD_MAX_SUBCODE_DATA;
}