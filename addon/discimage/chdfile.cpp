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
      m_cachedHunkNum(UINT32_MAX), // Start with an invalid hunk number
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

    // Open the CHD file
    chd_error err = chd_open(m_chd_filename, CHD_OPEN_READ, nullptr, &m_chd);
    if (err != CHDERR_NONE)
    {
        LOGERR("Failed to open CHD file: %s (error: %d)", m_chd_filename, err);
        return false;
    }

    // Get CHD header info
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

    // Allocate hunk buffer
    m_hunkSize = header->hunkbytes;
    m_hunkBuffer = new u8[m_hunkSize];
    if (!m_hunkBuffer)
    {
        LOGERR("Failed to allocate hunk buffer");
        chd_close(m_chd);
        m_chd = nullptr;
        return false;
    }

    // Parse track metadata
    if (!ParseTrackMetadata())
    {
        LOGERR("No CD-ROM tracks found in CHD");
        chd_close(m_chd);
        m_chd = nullptr;
        return false;
    }

    LOGNOTE("CHD has %d tracks", m_numTracks);

    // Determine frame size from first track
    m_frameSize = m_tracks[0].dataSize;

    // Check for subchannel data in the CHD
    bool hasPhysicalSubchannels = (header->unitbytes == CD_FRAME_SIZE); // 2448 = has subchannel

    // Check if filename contains .subchan. flag to force enable subchannels
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

int CCHDFileDevice::Read(void *pBuffer, size_t nCount)
{
    if (!m_chd || !pBuffer)
        return -1;

    const chd_header *header = chd_get_header(m_chd);
    if (!header)
        return -1;

    u32 unitBytes = header->unitbytes;    // 2448 bytes (2352 + 96 subchannel)
    u32 sectorBytes = CD_MAX_SECTOR_DATA; // 2352 bytes (audio/data only)
    u32 framesPerHunk = header->hunkbytes / unitBytes;

    size_t bytesRead = 0;
    u8 *dest = static_cast<u8 *>(pBuffer);

    while (bytesRead < nCount)
    {
        // Calculate which frame and offset
        u64 absoluteFrame = m_currentOffset / sectorBytes;
        u32 offsetInSector = m_currentOffset % sectorBytes;

        // Calculate hunk
        u32 hunkNum = absoluteFrame / framesPerHunk;
        u32 frameInHunk = absoluteFrame % framesPerHunk;

        // Read the hunk if it's not already cached
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

        // Position within hunk: frame start + offset within sector
        u32 frameStartInHunk = frameInHunk * unitBytes;
        u32 readPosition = frameStartInHunk + offsetInSector;

        // DEBUG CODE - log first audio frame
        // if (absoluteFrame == 2912 && bytesRead == 0)
        // {
        //     LOGDBG("First audio frame: hunk=%u, frameInHunk=%u, readPos=%u",
        //             hunkNum, frameInHunk, readPosition);
        //     LOGDBG("First 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
        //             hunkBuf[readPosition + 0], hunkBuf[readPosition + 1], hunkBuf[readPosition + 2], hunkBuf[readPosition + 3],
        //             hunkBuf[readPosition + 4], hunkBuf[readPosition + 5], hunkBuf[readPosition + 6], hunkBuf[readPosition + 7],
        //             hunkBuf[readPosition + 8], hunkBuf[readPosition + 9], hunkBuf[readPosition + 10], hunkBuf[readPosition + 11],
        //             hunkBuf[readPosition + 12], hunkBuf[readPosition + 13], hunkBuf[readPosition + 14], hunkBuf[readPosition + 15]);
        // }

        // How much can we read from this sector?
        u32 bytesLeftInSector = sectorBytes - offsetInSector;
        u32 bytesToCopy = nCount - bytesRead;
        if (bytesToCopy > bytesLeftInSector)
        {
            bytesToCopy = bytesLeftInSector;
        }

        // Copy data (skip subchannel at end of frame)
        memcpy(dest + bytesRead, m_hunkBuffer + readPosition, bytesToCopy);

        // Byte-swap ONLY for audio tracks (CHD stores audio in different endianness than BIN)
        // Determine which track we're in, with caching
        u64 currentLBA = m_currentOffset / sectorBytes;
        u64 lastTrackEnd = m_tracks[m_lastTrackIndex].startLBA + m_tracks[m_lastTrackIndex].frames;

        if (currentLBA < m_tracks[m_lastTrackIndex].startLBA || currentLBA >= lastTrackEnd)
        {
            // LBA is outside of the last track, so we need to search for the correct one
            m_lastTrackIndex = -1; // Invalidate last track index
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
            // Swap bytes for audio data
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
    // CHD files are read-only
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
    
    // Always return size based on sector data only (2352 bytes), not including subchannel
    u64 totalFrames = header->logicalbytes / header->unitbytes;
    return totalFrames * CD_MAX_SECTOR_DATA;  // 2352 bytes per frame
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
    // Track numbers are 1-based
    if (track < 1 || track > m_numTracks)
        return 0;
    int index = track - 1;
    return m_tracks[index].startLBA;
}

u32 CCHDFileDevice::GetTrackLength(int track) const
{
    // Track numbers are 1-based
    if (track < 1 || track > m_numTracks)
        return 0;
    int index = track - 1;
    return m_tracks[index].frames;
}

bool CCHDFileDevice::IsAudioTrack(int track) const
{
    // Track numbers are 1-based (Track 1, Track 2, etc.)
    // Convert to 0-based array index
    if (track < 1 || track > m_numTracks)
        return false;
    
    int index = track - 1;  // Convert 1-based track to 0-based index
    return m_tracks[index].trackType == CD_TRACK_AUDIO;
}

int CCHDFileDevice::ReadSubchannel(u32 lba, u8 *subchannel)
{
    if (!m_hasSubchannels || !subchannel)
        return -1;

    // Calculate which hunk contains this LBA
    const chd_header *header = chd_get_header(m_chd);
    u32 framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;
    u32 hunkNum = lba / framesPerHunk;
    u32 frameInHunk = lba % framesPerHunk;

    // Read the hunk if it's not already cached
    if (hunkNum != m_cachedHunkNum)
    {
        chd_error err = chd_read(m_chd, hunkNum, m_hunkBuffer);
        if (err != CHDERR_NONE)
        {
            LOGERR("CHD read error at hunk %u: %d", hunkNum, err);
            return -1;
        }
        m_cachedHunkNum = hunkNum;
    }

    // Subchannel data is at the end of each frame
    u32 frameOffset = frameInHunk * CD_FRAME_SIZE;
    memcpy(subchannel, m_hunkBuffer + frameOffset + CD_MAX_SECTOR_DATA, CD_MAX_SUBCODE_DATA);

    // DEBUG: Log first subchannel read
    if (lba == 0) {
        LOGNOTE("ReadSubchannel LBA=0, first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                subchannel[0], subchannel[1], subchannel[2], subchannel[3],
                subchannel[4], subchannel[5], subchannel[6], subchannel[7],
                subchannel[8], subchannel[9], subchannel[10], subchannel[11],
                subchannel[12], subchannel[13], subchannel[14], subchannel[15]);
    }

    return CD_MAX_SUBCODE_DATA;
}
