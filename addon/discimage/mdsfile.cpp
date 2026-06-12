#include "mdsfile.h"
#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "util.h"
#include "../mdsparser/mdsparser.h"

LOGMODULE("CMDSFileDevice");

// MDS track mode: the low nibble decodes the type, with values 8-15
// equivalent to 0-7 (verified against libmirage image-mds).
enum class MDSTrackType { Unknown, Audio, Mode1, Mode2, Mode2Form1, Mode2Form2 };

static MDSTrackType DecodeMDSMode(u8 mode)
{
    switch (mode & 0x07) {
        case 0x00: return MDSTrackType::Mode2;
        case 0x01: return MDSTrackType::Audio;
        case 0x02: return MDSTrackType::Mode1;
        case 0x03: return MDSTrackType::Mode2;
        case 0x04: return MDSTrackType::Mode2Form1;
        case 0x05: return MDSTrackType::Mode2Form2;
        case 0x07: return MDSTrackType::Mode2;
        default:   return MDSTrackType::Unknown;
    }
}

// Cue TRACK type from the decoded mode and the logical (subchannel-less)
// sector size; this is what determines the gadget's skip_bytes/block_size.
static const char *CueTypeFor(MDSTrackType type, u32 baseSize)
{
    switch (type) {
        case MDSTrackType::Audio:
            return "AUDIO";
        case MDSTrackType::Mode1:
            return (baseSize == 2048) ? "MODE1/2048" : "MODE1/2352";
        case MDSTrackType::Mode2:
        case MDSTrackType::Mode2Form1:
        case MDSTrackType::Mode2Form2:
            if (baseSize == 2336) return "MODE2/2336";
            if (baseSize == 2324) return "MODE2/2324";
            if (baseSize == 2048) return "MODE2/2048";
            return "MODE2/2352";
        default:
            return "MODE1/2352";
    }
}

CMDSFileDevice::CMDSFileDevice(const char* mds_filename, char *mds_str, MEDIA_TYPE mediaType) :
    m_mds_str(mds_str),
    m_mds_filename(mds_filename),
    m_mediaType(mediaType)
{
}

bool CMDSFileDevice::Init() {
    m_parser = new MDSParser(m_mds_str);
    if (!m_parser->isValid()) {
        LOGERR("Invalid MDS file");
        return false;
    }

    LOGNOTE("MDS: %d session(s)", m_parser->getNumSessions());

    // Open MDF file
    const char* mdf_filename_from_mds = m_parser->getMDFilename();
    LOGNOTE("MDF filename from parser: %s", mdf_filename_from_mds);
    char mdf_path[255];

    if (strcmp(mdf_filename_from_mds, "*.mdf") == 0) {
        // Handle wildcard filename - derive from MDS filename
        const char* extension = strrchr(m_mds_filename, '.');
        if (extension) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s.mdf",
                    (int)(extension - m_mds_filename), m_mds_filename);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s.mdf", m_mds_filename);
        }
    } else {
        // Use the filename from MDS, relative to the MDS directory
        const char* last_slash = strrchr(m_mds_filename, '/');
        if (last_slash) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s%s",
                    (int)(last_slash - m_mds_filename + 1), m_mds_filename, mdf_filename_from_mds);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s", mdf_filename_from_mds);
        }
    }

    LOGNOTE("Attempting to open MDF file at: %s", mdf_path);
    m_pFile = new FIL();
    FRESULT result = f_open(m_pFile, mdf_path, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open MDF file for reading (FatFs error %d)", result);
        delete m_pFile;
        m_pFile = nullptr;
        return false;
    }
    if (m_pFile) {
        FatFsOptimizer::EnableFastSeek(m_pFile, &m_pCLMT, 256, "MDS: ");
    }

    if (!BuildTrackMap())
        return false;

    if (!GenerateCueSheet())
        return false;

    // Detect subchannel data (SafeDisc support)
    m_hasSubchannels = false;
    for (int i = 0; i < m_nMapCount; i++) {
        if (m_Map[i].subchannel != 0) {
            m_hasSubchannels = true;
            LOGNOTE("Track %d has subchannel data (sector_size %u)",
                    m_Map[i].track_number, m_Map[i].sector_size);
        }
    }
    LOGNOTE("Image has subchannel data: %s",
            m_hasSubchannels ? "YES (SafeDisc compatible)" : "NO");

    return true;
}

bool CMDSFileDevice::BuildTrackMap() {
    m_nMapCount = 0;

    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);

        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);

            // Only process actual track entries (point is the track number);
            // skip lead-in entries (0xA0, 0xA1, 0xA2)
            if (track->point == 0 || track->point >= 0xA0)
                continue;

            if (m_nMapCount >= MaxTracks) {
                LOGERR("Too many tracks in MDS");
                return false;
            }

            TrackMap &m = m_Map[m_nMapCount];
            m.sector_size = (track->sector_size != 0) ? track->sector_size : 2352;
            m.subchannel = track->subchannel;
            m.base_size = (m.subchannel != 0 && m.sector_size > 96)
                              ? m.sector_size - 96 : m.sector_size;
            m.data_start = track->start_sector;
            m.start_offset = track->start_offset;
            m.length = extra ? extra->length : 0;
            m.session = session->session_number != 0 ? session->session_number : (i + 1);
            m.track_number = track->point;
            MDSTrackType type = DecodeMDSMode(track->mode);
            m.audio = (type == MDSTrackType::Audio);
            m.cue_type = CueTypeFor(type, m.base_size);

            // Virtual byte position per the generated cue's accounting:
            // consecutive INDEX 01 entries, previous track's sector length
            if (m_nMapCount == 0) {
                m.vstart = 0;
            } else {
                const TrackMap &prev = m_Map[m_nMapCount - 1];
                m.vstart = prev.vstart +
                           (u64)(m.data_start - prev.data_start) * prev.base_size;
            }

            LOGNOTE("Track %d: session %d, mode 0x%02x -> %s, LBA %u, len %u, "
                    "sector %u/%u, file offset %llu, vstart %llu",
                    m.track_number, m.session, track->mode, m.cue_type,
                    m.data_start, m.length, m.base_size, m.sector_size,
                    (unsigned long long)m.start_offset, (unsigned long long)m.vstart);

            m_nMapCount++;
        }
    }

    if (m_nMapCount == 0) {
        LOGERR("No tracks found in MDS");
        return false;
    }

    // Last track length: trust the extra block, else derive from MDF size
    TrackMap &last = m_Map[m_nMapCount - 1];
    if (last.length == 0 && last.sector_size != 0) {
        u64 fileSize = f_size(m_pFile);
        if (fileSize > last.start_offset)
            last.length = (u32)((fileSize - last.start_offset) / last.sector_size);
    }

    m_vsize = last.vstart + (u64)last.length * last.base_size;
    m_vpos = 0;

    LOGNOTE("MDS map: %d track(s), virtual size %llu, leadout LBA %u",
            m_nMapCount, (unsigned long long)m_vsize, last.data_start + last.length);

    return true;
}

bool CMDSFileDevice::GenerateCueSheet() {
    // Normalized cue: single FILE, absolute INDEX 01 times, REM SESSION
    // markers, no PREGAP (MDS start_sector values are disc-absolute and
    // already include inter-session gaps)
    size_t bufSize = 128 + (size_t)m_nMapCount * 100;
    char *buf = new char[bufSize];
    char *p = buf;
    size_t remaining = bufSize;
    int session = 0;

    int len = snprintf(p, remaining, "FILE \"mdf\" BINARY\n");
    p += len;
    remaining -= len;

    for (int i = 0; i < m_nMapCount && remaining > 100; i++) {
        const TrackMap &m = m_Map[i];

        if (m.session != session) {
            session = m.session;
            len = snprintf(p, remaining, "REM SESSION %02d\n", session);
            p += len;
            remaining -= len;
        }

        len = snprintf(p, remaining, "  TRACK %02d %s\n", m.track_number, m.cue_type);
        p += len;
        remaining -= len;

        len = snprintf(p, remaining, "    INDEX 01 %02u:%02u:%02u\n",
                       m.data_start / (75 * 60), (m.data_start / 75) % 60, m.data_start % 75);
        p += len;
        remaining -= len;
    }

    m_cue_sheet = buf;
    LOGNOTE("Generated CUE sheet:\n%s", m_cue_sheet);
    return true;
}

CMDSFileDevice::~CMDSFileDevice(void) {
    FatFsOptimizer::DisableFastSeek(&m_pCLMT);
    if (m_pFile) {
        f_close(m_pFile);
        delete m_pFile;
        m_pFile = nullptr;
    }

    if (m_mds_str != nullptr) {
        delete[] m_mds_str;
        m_mds_str = nullptr;
    }

    if (m_cue_sheet != nullptr) {
        delete[] m_cue_sheet;
        m_cue_sheet = nullptr;
    }

    delete m_parser;
}

int CMDSFileDevice::FindMapForV(u64 vpos) const {
    for (int i = 0; i < m_nMapCount; i++) {
        u64 vend = (i + 1 < m_nMapCount) ? m_Map[i + 1].vstart : m_vsize;
        if (vpos >= m_Map[i].vstart && vpos < vend)
            return i;
    }
    return -1;
}

int CMDSFileDevice::FindMapForLBA(u32 lba) const {
    for (int i = 0; i < m_nMapCount; i++) {
        u32 end = (i + 1 < m_nMapCount) ? m_Map[i + 1].data_start
                                        : m_Map[i].data_start + m_Map[i].length;
        if (lba >= m_Map[i].data_start && lba < end)
            return i;
    }
    return -1;
}

int CMDSFileDevice::Read(void *pBuffer, size_t nSize) {
    if (!m_pFile) {
        LOGERR("Read !m_pFile");
        return -1;
    }

    u8 *dest = static_cast<u8 *>(pBuffer);
    size_t total = 0;

    while (total < nSize && m_vpos < m_vsize) {
        int ti = FindMapForV(m_vpos);
        if (ti < 0)
            break;

        const TrackMap &t = m_Map[ti];
        u64 vend = (ti + 1 < m_nMapCount) ? m_Map[ti + 1].vstart : m_vsize;
        u64 rel = m_vpos - t.vstart;
        u64 sector = rel / t.base_size;
        u32 rem = (u32)(rel % t.base_size);

        size_t chunk = nSize - total;
        if (chunk > t.base_size - rem)
            chunk = t.base_size - rem;
        if (m_vpos + chunk > vend)
            chunk = (size_t)(vend - m_vpos);

        if (t.length != 0 && sector >= t.length) {
            // Between this track's data and the next track: not stored in
            // the MDF (inter-session gap), reads as zeros
            memset(dest + total, 0, chunk);
        } else {
            u64 phys = t.start_offset + sector * t.sector_size + rem;
            if (f_tell(m_pFile) != phys) {
                FRESULT result = f_lseek(m_pFile, phys);
                if (result != FR_OK) {
                    LOGERR("Seek to %llu failed, err %d", (unsigned long long)phys, result);
                    return total > 0 ? (int)total : -1;
                }
            }
            UINT got = 0;
            FRESULT result = f_read(m_pFile, dest + total, chunk, &got);
            if (result != FR_OK) {
                LOGERR("Read at %llu failed, err %d", (unsigned long long)phys, result);
                return total > 0 ? (int)total : -1;
            }
            if (got < chunk) {
                total += got;
                m_vpos += got;
                break;
            }
        }

        total += chunk;
        m_vpos += chunk;
    }

    return (int)total;
}

int CMDSFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    return -1;
}

u64 CMDSFileDevice::Tell() const {
    return m_vpos;
}

u64 CMDSFileDevice::Seek(u64 nOffset) {
    m_vpos = nOffset;
    return nOffset;
}

u64 CMDSFileDevice::GetSize(void) const {
    return m_vsize;
}

int CMDSFileDevice::GetNumTracks() const {
    return m_nMapCount;
}

u32 CMDSFileDevice::GetTrackStart(int track) const {
    if (track < 0 || track >= m_nMapCount)
        return 0;
    return m_Map[track].data_start;
}

u32 CMDSFileDevice::GetTrackLength(int track) const {
    if (track < 0 || track >= m_nMapCount)
        return 0;
    return m_Map[track].length;
}

bool CMDSFileDevice::IsAudioTrack(int track) const {
    if (track < 0 || track >= m_nMapCount)
        return false;
    return m_Map[track].audio;
}

int CMDSFileDevice::ReadSubchannel(u32 lba, u8* subchannel) {
    if (!subchannel || !m_pFile || !m_hasSubchannels)
        return -1;

    int ti = FindMapForLBA(lba);
    if (ti < 0) {
        LOGERR("ReadSubchannel: LBA %u not in any track", lba);
        return -1;
    }

    const TrackMap &t = m_Map[ti];
    if (t.subchannel == 0)
        return -1;

    // The 96 subchannel bytes (raw interleaved P-W) follow each sector's
    // main data in the MDF
    u64 phys = t.start_offset + (u64)(lba - t.data_start) * t.sector_size + t.base_size;

    FRESULT result = f_lseek(m_pFile, phys);
    if (result != FR_OK) {
        LOGERR("Failed to seek to subchannel at LBA %u (offset %llu)",
               lba, (unsigned long long)phys);
        return -1;
    }

    UINT bytes_read;
    result = f_read(m_pFile, subchannel, 96, &bytes_read);
    if (result != FR_OK || bytes_read != 96) {
        LOGERR("Failed to read subchannel at LBA %u (read %u bytes)", lba, bytes_read);
        return -1;
    }

    return 96;
}
