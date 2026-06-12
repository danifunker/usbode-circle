#include "chdfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdio.h>

LOGMODULE("chdfile");

CCHDFileDevice::CCHDFileDevice(const char *chd_filename, MEDIA_TYPE mediaType)
    : m_chd_filename(chd_filename),
      m_mediaType(mediaType),
      m_currentOffset(0),
      m_vsize(0),
      m_hunkBuffer(nullptr),
      m_hunkSize(0),
      m_cachedHunkNum(UINT32_MAX)
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
            // Parse PREGAP if present
            track.pregap = 0;
            track.pregapStored = false;
            char *pPregap = strstr(metadata, "PREGAP:");
            if (pPregap)
            {
                sscanf(pPregap, "PREGAP:%u", &track.pregap);
            }

            // PGTYPE starting with 'V' means the pregap frames are stored
            // in the CHD as part of this track's FRAMES (MAME semantics)
            char *pPgType = strstr(metadata, "PGTYPE:");
            if (pPgType)
            {
                track.pregapStored = (pPgType[7] == 'V' || pPgType[7] == 'v');
            }

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

            LOGNOTE("Track %d: Type=%s (%d), Frames=%u, Pregap=%u (%s), DataSize=%u",
                    track.trackNumber, typeStr, track.trackType,
                    track.frames, track.pregap,
                    track.pregapStored ? "stored" : "unstored", track.dataSize);

            m_numTracks++;
        }

        metadata_index++;
    }

    if (m_numTracks == 0)
        return false;

    // Offset accounting (MAME cdrom_file semantics): logical LBAs include
    // unstored pregaps; each track's stored frames are padded to a 4-frame
    // boundary in the CHD; vstart mirrors the generated cue's byte
    // accounting so the gadget's per-track seeks land correctly.
    u32 logofs = 0;
    u32 chdofs = 0;
    for (int i = 0; i < m_numTracks; i++)
    {
        CHDTrackInfo &t = m_tracks[i];

        if (t.pregapStored)
        {
            t.track_start = logofs;
            t.data_start = logofs + t.pregap;
            logofs = t.track_start + t.frames; // frames include the pregap
        }
        else
        {
            logofs += t.pregap; // pregap occupies LBAs but no storage
            t.track_start = logofs;
            t.data_start = logofs;
            logofs = t.data_start + t.frames;
        }

        t.chdFrameStart = chdofs;
        chdofs += t.frames;
        chdofs += (t.frames + 3) / 4 * 4 - t.frames; // CD_TRACK_PADDING

        if (i == 0)
        {
            t.vstart = 0;
        }
        else
        {
            const CHDTrackInfo &prev = m_tracks[i - 1];
            u64 prevVData = prev.vstart + (u64)(prev.data_start - prev.track_start) * prev.dataSize;
            t.vstart = prevVData + (u64)(t.track_start - prev.data_start) * prev.dataSize;
        }

        LOGNOTE("Track %d: track_start=%u, data_start=%u, chdFrame=%u, vstart=%llu",
                t.trackNumber, t.track_start, t.data_start, t.chdFrameStart,
                (unsigned long long)t.vstart);
    }

    const CHDTrackInfo &last = m_tracks[m_numTracks - 1];
    m_vsize = last.vstart + (u64)last.frames * last.dataSize;

    return true;
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

        // Track mode string: must match the logical bytes per frame so the
        // gadget derives the right block size and header skip
        const char *mode;
        switch (track.trackType)
        {
        case CD_TRACK_AUDIO:          mode = "AUDIO";      break;
        case CD_TRACK_MODE1:          mode = "MODE1/2048"; break;
        case CD_TRACK_MODE1_RAW:      mode = "MODE1/2352"; break;
        case CD_TRACK_MODE2:          mode = "MODE2/2336"; break;
        case CD_TRACK_MODE2_FORM1:    mode = "MODE2/2048"; break;
        case CD_TRACK_MODE2_FORM2:    mode = "MODE2/2324"; break;
        case CD_TRACK_MODE2_FORM_MIX: mode = "MODE2/2336"; break;
        case CD_TRACK_MODE2_RAW:      mode = "MODE2/2352"; break;
        default:                      mode = "MODE1/2352"; break;
        }

        // Write TRACK line
        written = snprintf(buf, remaining, "  TRACK %02d %s\n", track.trackNumber, mode);
        buf += written;
        remaining -= written;

        // INDEX 00 only when the pregap frames are stored in the CHD
        // (unstored pregaps are not part of the byte space)
        if (track.track_start < track.data_start)
        {
            written = snprintf(buf, remaining, "    INDEX 00 %02u:%02u:%02u\n",
                               track.track_start / (60 * 75),
                               (track.track_start / 75) % 60,
                               track.track_start % 75);
            buf += written;
            remaining -= written;
        }

        written = snprintf(buf, remaining, "    INDEX 01 %02u:%02u:%02u\n",
                           track.data_start / (60 * 75),
                           (track.data_start / 75) % 60,
                           track.data_start % 75);
        buf += written;
        remaining -= written;
    }

    LOGNOTE("Generated CUE sheet with %d tracks:\n%s", m_numTracks, m_cue_sheet);
}

int CCHDFileDevice::Read(void *pBuffer, size_t nCount)
{
    if (!m_chd || !pBuffer)
        return -1;

    const chd_header *header = chd_get_header(m_chd);
    if (!header)
        return -1;

    u32 unitBytes = header->unitbytes;
    u32 framesPerHunk = header->hunkbytes / unitBytes;

    size_t bytesRead = 0;
    u8 *dest = static_cast<u8 *>(pBuffer);

    while (bytesRead < nCount && m_currentOffset < m_vsize)
    {
        // Find the track whose virtual byte range contains the position
        int ti = -1;
        for (int i = 0; i < m_numTracks; i++)
        {
            u64 vend = (i + 1 < m_numTracks) ? m_tracks[i + 1].vstart : m_vsize;
            if (m_currentOffset >= m_tracks[i].vstart && m_currentOffset < vend)
            {
                ti = i;
                break;
            }
        }
        if (ti < 0)
            break;

        const CHDTrackInfo &t = m_tracks[ti];
        u64 vend = (ti + 1 < m_numTracks) ? m_tracks[ti + 1].vstart : m_vsize;
        u64 rel = m_currentOffset - t.vstart;
        u64 frameIdx = rel / t.dataSize;
        u32 rem = (u32)(rel % t.dataSize);

        size_t bytesToCopy = nCount - bytesRead;
        if (bytesToCopy > t.dataSize - rem)
            bytesToCopy = t.dataSize - rem;
        if (m_currentOffset + bytesToCopy > vend)
            bytesToCopy = (size_t)(vend - m_currentOffset);

        if (frameIdx >= t.frames)
        {
            // Region between this track's stored frames and the next track
            // (unstored pregap/gap): zeros
            memset(dest + bytesRead, 0, bytesToCopy);
        }
        else
        {
            u32 chdFrame = t.chdFrameStart + (u32)frameIdx;
            u32 hunkNum = chdFrame / framesPerHunk;
            u32 frameInHunk = chdFrame % framesPerHunk;

            if (hunkNum != m_cachedHunkNum)
            {
                chd_error err = chd_read(m_chd, hunkNum, m_hunkBuffer);
                if (err != CHDERR_NONE)
                {
                    LOGERR("CHD read error at hunk %u: %d", hunkNum, err);
                    return bytesRead > 0 ? (int)bytesRead : -1;
                }
                m_cachedHunkNum = hunkNum;
            }

            memcpy(dest + bytesRead, m_hunkBuffer + frameInHunk * unitBytes + rem, bytesToCopy);

            // CHD stores audio big-endian; hosts expect little-endian.
            // Reads are sector-aligned in practice, so pairs line up.
            if (t.trackType == CD_TRACK_AUDIO)
            {
                for (size_t i = 0; i + 1 < bytesToCopy; i += 2)
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

    return (int)bytesRead;
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
    return m_vsize;
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
    return m_tracks[track].data_start;
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

    // Map the LBA to its CHD frame through the track table (the old code
    // used the LBA directly, ignoring padding and pregap offsets)
    int ti = -1;
    for (int i = 0; i < m_numTracks; i++)
    {
        u32 end = (i + 1 < m_numTracks) ? m_tracks[i + 1].track_start
                                        : m_tracks[i].track_start + m_tracks[i].frames;
        if (lba >= m_tracks[i].track_start && lba < end)
        {
            ti = i;
            break;
        }
    }
    if (ti < 0)
        return -1;

    u32 frameIdx = lba - m_tracks[ti].track_start;
    if (frameIdx >= m_tracks[ti].frames)
        return -1; // unstored gap

    u32 chdFrame = m_tracks[ti].chdFrameStart + frameIdx;

    const chd_header *header = chd_get_header(m_chd);
    u32 framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;
    u32 hunkNum = chdFrame / framesPerHunk;
    u32 frameInHunk = chdFrame % framesPerHunk;

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