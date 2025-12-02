#include "chdfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdio.h>

LOGMODULE("chdfile");

CCHDFileDevice::CCHDFileDevice(const char *chd_filename, MEDIA_TYPE mediaType)
    : m_chd_filename(chd_filename),
      m_mediaType(mediaType),
      m_hunkSize(0),
      m_accessCounter(0),
      m_lastTrackIndex(0)
{
    LOGNOTE("CCHDFileDevice created for: %s", chd_filename);
    memset(m_tracks, 0, sizeof(m_tracks));
    for (int i = 0; i < CHD_CACHE_SIZE; ++i) {
        m_cache[i].hunkNum = 0;
        m_cache[i].buffer = nullptr;
        m_cache[i].lastUsed = 0;
        m_cache[i].valid = false;
    }
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
    for (int i = 0; i < CHD_CACHE_SIZE; ++i) {
        if (m_cache[i].buffer) {
            delete[] m_cache[i].buffer;
            m_cache[i].buffer = nullptr;
        }
    }
}

bool CCHDFileDevice::ParseTrackMetadata()
{
    // Get track metadata from CHD
    char metadata[256];
    uint32_t metadata_length = sizeof(metadata);
    uint32_t metadata_index = 0;

    m_numTracks = 0;

    // Iterate through all CD track metadata tags
    while (m_numTracks < CD_MAX_TRACKS)
    {
        chd_error err = chd_get_metadata(m_chd, CDROM_TRACK_METADATA2_TAG,
                                         metadata_index, metadata, metadata_length,
                                         nullptr, nullptr, nullptr);

        if (err != CHDERR_NONE)
        {
            // Try old metadata format
            err = chd_get_metadata(m_chd, CDROM_TRACK_METADATA_TAG,
                                   metadata_index, metadata, metadata_length,
                                   nullptr, nullptr, nullptr);
            if (err != CHDERR_NONE)
                break;
        }

        // Parse metadata string
        CHDTrackInfo &track = m_tracks[m_numTracks];
        char typeStr[32];

        // Try CHT2 format first (newer format with PREGAP)
        if (sscanf(metadata, "TRACK:%u TYPE:%31s SUBTYPE:%*s FRAMES:%u",
                   &track.trackNumber, typeStr, &track.frames) == 3)
        {

            track.startLBA = (m_numTracks > 0) ? (m_tracks[m_numTracks - 1].startLBA + m_tracks[m_numTracks - 1].frames) : 0;

            // Parse type string to track type enum
            if (strcmp(typeStr, "AUDIO") == 0)
            {
                track.trackType = CD_TRACK_AUDIO;
                track.dataSize = 2352;
            }
            else if (strcmp(typeStr, "MODE1") == 0 || strcmp(typeStr, "MODE1_2048") == 0)
            {
                track.trackType = CD_TRACK_MODE1;
                track.dataSize = 2048;
            }
            else if (strcmp(typeStr, "MODE1_RAW") == 0 || strcmp(typeStr, "MODE1_2352") == 0)
            {
                track.trackType = CD_TRACK_MODE1_RAW;
                track.dataSize = 2352;
            }
            else if (strcmp(typeStr, "MODE2") == 0 || strcmp(typeStr, "MODE2_2336") == 0)
            {
                track.trackType = CD_TRACK_MODE2;
                track.dataSize = 2336;
            }
            else if (strcmp(typeStr, "MODE2_FORM1") == 0 || strcmp(typeStr, "MODE2_2048") == 0)
            {
                track.trackType = CD_TRACK_MODE2_FORM1;
                track.dataSize = 2048;
            }
            else if (strcmp(typeStr, "MODE2_FORM2") == 0 || strcmp(typeStr, "MODE2_2324") == 0)
            {
                track.trackType = CD_TRACK_MODE2_FORM2;
                track.dataSize = 2324;
            }
            else if (strcmp(typeStr, "MODE2_FORM_MIX") == 0)
            {
                track.trackType = CD_TRACK_MODE2_FORM_MIX;
                track.dataSize = 2336;
            }
            else if (strcmp(typeStr, "MODE2_RAW") == 0 || strcmp(typeStr, "MODE2_2352") == 0)
            {
                track.trackType = CD_TRACK_MODE2_RAW;
                track.dataSize = 2352;
            }
            else
            {
                // Default to MODE1_RAW if unknown
                LOGWARN("Unknown track type: %s, defaulting to MODE1_RAW", typeStr);
                track.trackType = CD_TRACK_MODE1_RAW;
                track.dataSize = 2352;
            }

            LOGNOTE("Track %d: Type=%s (%d), Start=%u, Frames=%u, DataSize=%u",
                    track.trackNumber, typeStr, track.trackType, track.startLBA,
                    track.frames, track.dataSize);

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

    LOGNOTE("CHD version: %d, hunk size: %d bytes",
            header->version, header->hunkbytes);

    m_hunkSize = header->hunkbytes;

    // Allocate cache buffers
    for (int i = 0; i < CHD_CACHE_SIZE; ++i) {
        m_cache[i].buffer = new u8[m_hunkSize];
        if (!m_cache[i].buffer) {
             LOGERR("Failed to allocate hunk buffer %d", i);
             // Cleanup already allocated
             for (int j=0; j<i; ++j) {
                 delete[] m_cache[j].buffer;
                 m_cache[j].buffer = nullptr;
             }
             chd_close(m_chd);
             m_chd = nullptr;
             return false;
        }
        m_cache[i].valid = false;
        m_cache[i].lastUsed = 0;
    }

    if (!ParseTrackMetadata())
    {
        LOGERR("No CD-ROM tracks found in CHD");
        // Cleanup happens in destructor if this returns false
        return false;
    }

    LOGNOTE("CHD has %d tracks", m_numTracks);
    m_frameSize = m_tracks[0].dataSize;

    bool hasPhysicalSubchannels = (header->unitbytes == CD_FRAME_SIZE);
    bool forceEnableSubchannels = (strstr(m_chd_filename, ".subchan.") != nullptr);

    if (hasPhysicalSubchannels)
    {
        if (forceEnableSubchannels)
        {
            LOGNOTE("CHD contains subchannel data - ENABLED (forced by .subchan. in filename)");
            m_hasSubchannels = true;
        }
        else
        {
            LOGNOTE("CHD contains subchannel data (likely synthesized by chdman)");
            LOGNOTE("Disabling subchannel reporting for compatibility - add .subchan. to filename to force enable");
            m_hasSubchannels = false;
        }
    }
    else
    {
        LOGNOTE("CHD does not contain subchannel data");
        m_hasSubchannels = false;
    }

    // Generate CUE sheet for compatibility
    GenerateCueSheet();

    return true;
}

void CCHDFileDevice::GenerateCueSheet()
{
    // Allocate buffer for CUE sheet (generous size)
    size_t bufSize = 4096;
    m_cue_sheet = new char[bufSize];
    char *buf = m_cue_sheet;
    size_t remaining = bufSize;

    // Write FILE line
    int written = snprintf(buf, remaining, "FILE \"%s\" BINARY\n", m_chd_filename);
    buf += written;
    remaining -= written;

    // Write track entries
    for (int i = 0; i < m_numTracks && remaining > 100; i++)
    {
        const CHDTrackInfo &track = m_tracks[i];

        // Determine track mode string
        const char *mode;
        if (track.trackType == CD_TRACK_AUDIO)
        {
            mode = "AUDIO";
        }
        else if (track.dataSize == 2048)
        {
            mode = "MODE1/2048";
        }
        else
        {
            mode = "MODE1/2352";
        }

        // Calculate MSF for track start
        u32 lba = track.startLBA;
        
        u32 minutes = lba / (60 * 75);
        u32 seconds = (lba / 75) % 60;
        u32 frames = lba % 75;

        written = snprintf(buf, remaining,
                           "  TRACK %02d %s\n"
                           "    INDEX 01 %02d:%02d:%02d\n",
                           track.trackNumber, mode,
                           minutes, seconds, frames);
        buf += written;
        remaining -= written;
    }

    LOGNOTE("Generated CUE sheet with %d tracks", m_numTracks);
}

u8* CCHDFileDevice::GetHunk(u32 hunkNum) {
    // Check cache
    for (int i = 0; i < CHD_CACHE_SIZE; ++i) {
        if (m_cache[i].valid && m_cache[i].hunkNum == hunkNum) {
            m_cache[i].lastUsed = ++m_accessCounter;
            return m_cache[i].buffer;
        }
    }

    // Find victim (Invalid or LRU)
    int victim = -1;
    u64 minAccess = ~0ULL; // UINT64_MAX

    for (int i = 0; i < CHD_CACHE_SIZE; ++i) {
        if (!m_cache[i].valid) {
            victim = i;
            break;
        }
        if (m_cache[i].lastUsed < minAccess) {
            minAccess = m_cache[i].lastUsed;
            victim = i;
        }
    }

    if (victim == -1) victim = 0; // Should not happen given logic above

    // Load hunk
    chd_error err = chd_read(m_chd, hunkNum, m_cache[victim].buffer);
    if (err != CHDERR_NONE) {
        LOGERR("CHD read error at hunk %u: %d", hunkNum, err);
        return nullptr;
    }

    m_cache[victim].hunkNum = hunkNum;
    m_cache[victim].valid = true;
    m_cache[victim].lastUsed = ++m_accessCounter;

    return m_cache[victim].buffer;
}

int CCHDFileDevice::Read(void *pBuffer, size_t nCount)
{
    if (!m_chd || !pBuffer)
        return -1;

    const chd_header *header = chd_get_header(m_chd);
    if (!header)
        return -1;

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

        u8* hunkBuf = GetHunk(hunkNum);
        if (!hunkBuf) return bytesRead > 0 ? bytesRead : -1;

        u32 frameStartInHunk = frameInHunk * unitBytes;
        u32 readPosition = frameStartInHunk + offsetInSector;

        u32 bytesLeftInSector = sectorBytes - offsetInSector;
        u32 bytesToCopy = nCount - bytesRead;
        if (bytesToCopy > bytesLeftInSector)
        {
            bytesToCopy = bytesLeftInSector;
        }

        // Determine track info for current position
        u64 currentLBA = absoluteFrame;
        u64 lastTrackEnd = m_tracks[m_lastTrackIndex].startLBA + m_tracks[m_lastTrackIndex].frames;
        
        // Optimizing track lookup
        if (currentLBA < m_tracks[m_lastTrackIndex].startLBA || currentLBA >= lastTrackEnd)
        {
             // Reset search
             bool found = false;
             // Check next track first (sequential access optimization)
             int nextTrack = m_lastTrackIndex + 1;
             if (nextTrack < m_numTracks) {
                 u64 nextEnd = m_tracks[nextTrack].startLBA + m_tracks[nextTrack].frames;
                 if (currentLBA >= m_tracks[nextTrack].startLBA && currentLBA < nextEnd) {
                     m_lastTrackIndex = nextTrack;
                     found = true;
                 }
             }

             if (!found) {
                // Linear search
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
        }

        if (m_lastTrackIndex >= 0 && m_tracks[m_lastTrackIndex].trackType == CD_TRACK_AUDIO)
        {
            // Optimized audio copy and swap
            const u8* src = hunkBuf + readPosition;
            u8* d = dest + bytesRead;

            // We process bytes 2 by 2
            // Since this is audio, we assume alignment is fine for byte access
            for (u32 i = 0; i < bytesToCopy; i += 2) {
                 if (i + 1 < bytesToCopy) {
                     // Swap bytes while copying
                     d[i] = src[i+1];
                     d[i+1] = src[i];
                 } else {
                     // Last odd byte (should not happen for full sector reads)
                     d[i] = src[i];
                 }
            }
        }
        else
        {
            memcpy(dest + bytesRead, hunkBuf + readPosition, bytesToCopy);
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
    if (track < 0 || track >= m_numTracks)
        return 0;
    return m_tracks[track].startLBA;
}

u32 CCHDFileDevice::GetTrackLength(int track) const
{
    if (track < 0 || track >= m_numTracks)
        return 0;
    return m_tracks[track].frames;
}

bool CCHDFileDevice::IsAudioTrack(int track) const
{
    if (track < 0 || track >= m_numTracks)
        return false;
    return m_tracks[track].trackType == CD_TRACK_AUDIO;
}

int CCHDFileDevice::ReadSubchannel(u32 lba, u8 *subchannel)
{
    if (!m_hasSubchannels || !subchannel)
        return -1;

    const chd_header *header = chd_get_header(m_chd);
    u32 framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;
    u32 hunkNum = lba / framesPerHunk;
    u32 frameInHunk = lba % framesPerHunk;

    u8* hunkBuf = GetHunk(hunkNum);
    if (!hunkBuf) return -1;

    // Subchannel data is at the end of each frame
    u32 frameOffset = frameInHunk * CD_FRAME_SIZE;
    memcpy(subchannel, hunkBuf + frameOffset + CD_MAX_SECTOR_DATA, CD_MAX_SUBCODE_DATA);

    return CD_MAX_SUBCODE_DATA;
}
