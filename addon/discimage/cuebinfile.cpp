//
// A CDevice for cue/bin files (single- and multi-file) and ISO images
//
// Copyright (C) 2025 Ian Cass, Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "cuebinfile.h"

#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <circle/timer.h>

LOGMODULE("CCueBinFileDevice");

// Standard inter-session gap: 6750 frames lead-out (1:30) after session 1,
// 2250 (0:30) after later sessions, plus 4500 frames lead-in (1:00).
static u32 SessionGapFrames(int fromSession)
{
    return (fromSession == 1) ? (6750 + 4500) : (2250 + 4500);
}

static const char *TrackModeString(CUETrackMode mode)
{
    switch (mode) {
        case CUETrack_AUDIO:      return "AUDIO";
        case CUETrack_CDG:        return "CDG";
        case CUETrack_MODE1_2048: return "MODE1/2048";
        case CUETrack_MODE1_2352: return "MODE1/2352";
        case CUETrack_MODE2_2048: return "MODE2/2048";
        case CUETrack_MODE2_2324: return "MODE2/2324";
        case CUETrack_MODE2_2336: return "MODE2/2336";
        case CUETrack_MODE2_2352: return "MODE2/2352";
        case CUETrack_CDI_2336:   return "CDI/2336";
        case CUETrack_CDI_2352:   return "CDI/2352";
    }
    return "MODE1/2352";
}

// Bytes of a file that the cue accounting actually addresses: everything up
// to the last whole sector of the last track (a trailing partial sector is
// not addressable and must not shift the virtual layout).
static u64 ConsumedBytes(u64 fileSize, u64 lastTrackFileOffset, u32 lastTrackSectorLen)
{
    if (lastTrackSectorLen == 0 || fileSize < lastTrackFileOffset)
        return lastTrackFileOffset;
    return lastTrackFileOffset + ((fileSize - lastTrackFileOffset) / lastTrackSectorLen) * lastTrackSectorLen;
}

CCueBinFileDevice::CCueBinFileDevice(const char *cuePath, const char *cue_str, MEDIA_TYPE mediaType)
    : m_mediaType(mediaType)
{
    m_FileType = FileType::CUEBIN;

    if (cue_str != nullptr) {
        size_t len = strlen(cue_str);
        m_SourceCue = new char[len + 1];
        strcpy(m_SourceCue, cue_str);
    }

    // Directory of the cue file, for resolving relative bin filenames
    if (cuePath != nullptr) {
        const char *lastSlash = strrchr(cuePath, '/');
        if (lastSlash != nullptr) {
            size_t prefixLen = lastSlash - cuePath + 1;
            if (prefixLen >= sizeof(m_DirPrefix))
                prefixLen = sizeof(m_DirPrefix) - 1;
            memcpy(m_DirPrefix, cuePath, prefixLen);
            m_DirPrefix[prefixLen] = '\0';
        }
    }
}

CCueBinFileDevice::CCueBinFileDevice(FIL *pFile, MEDIA_TYPE mediaType)
    : m_mediaType(mediaType)
{
    m_FileType = FileType::ISO;
    m_Files[0] = pFile;
    m_nFiles = 1;
}

CCueBinFileDevice::~CCueBinFileDevice(void)
{
    for (int i = 0; i < m_nFiles; i++) {
        FatFsOptimizer::DisableFastSeek(&m_CLMTs[i]);
        if (m_Files[i]) {
            f_close(m_Files[i]);
            delete m_Files[i];
            m_Files[i] = nullptr;
        }
    }

    delete[] m_SourceCue;
    delete[] m_NormalizedCue;
}

bool CCueBinFileDevice::Init()
{
    if (m_FileType == FileType::ISO) {
        // Single data file, no cue sheet on disk: one segment, default cue
        FIL *f = m_Files[0];
        if (!f) {
            LOGERR("Init: no file");
            return false;
        }
        FatFsOptimizer::EnableFastSeek(f, &m_CLMTs[0], 256, "BIN/ISO: ");

        m_Segments[0] = {0, f_size(f), 0, 0};
        m_nSegments = 1;
        m_vsize = f_size(f);

        size_t len = strlen(default_cue_sheet);
        m_NormalizedCue = new char[len + 1];
        strcpy(m_NormalizedCue, default_cue_sheet);

        memset(&m_Tracks[0], 0, sizeof(m_Tracks[0]));
        m_Tracks[0].track_number = 1;
        m_Tracks[0].session = 1;
        m_Tracks[0].track_mode = CUETrack_MODE1_2048;
        m_Tracks[0].sector_length = 2048;
        m_nTracks = 1;
        m_LeadoutLBA = m_vsize / 2048;
        return true;
    }

    if (!m_SourceCue) {
        LOGERR("Init: no cue sheet");
        return false;
    }

    if (!ParseAndBuild())
        return false;

    ScanCatalogISRC();

    return EmitNormalizedCue();
}

// CUEParser does not handle CATALOG/ISRC lines; pick them out of the
// source cue text directly.
void CCueBinFileDevice::ScanCatalogISRC()
{
    int curTrackIdx = -1;
    const char *p = m_SourceCue;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            p++;
        const char *lineEnd = strchr(p, '\n');

        if (strncasecmp(p, "CATALOG ", 8) == 0) {
            const char *v = p + 8;
            while (*v == ' ')
                v++;
            int n = 0;
            while (n < 13 && v[n] >= '0' && v[n] <= '9')
                n++;
            if (n == 13) {
                memcpy(m_MCN, v, 13);
                m_MCN[13] = '\0';
            }
        } else if (strncasecmp(p, "TRACK ", 6) == 0) {
            int trackNum = atoi(p + 6);
            curTrackIdx = -1;
            for (int i = 0; i < m_nTracks; i++) {
                if (m_Tracks[i].track_number == trackNum) {
                    curTrackIdx = i;
                    break;
                }
            }
        } else if (strncasecmp(p, "ISRC ", 5) == 0 && curTrackIdx >= 0) {
            const char *v = p + 5;
            while (*v == ' ')
                v++;
            int n = 0;
            while (n < 12 && v[n] != '\0' && v[n] != '\r' && v[n] != '\n' && v[n] != ' ')
                n++;
            if (n == 12) {
                memcpy(m_ISRC[curTrackIdx], v, 12);
                m_ISRC[curTrackIdx][12] = '\0';
            }
        }

        if (lineEnd == nullptr)
            break;
        p = lineEnd + 1;
    }
}

bool CCueBinFileDevice::GetMCN(char mcn[14]) const
{
    if (m_MCN[0] == '\0')
        return false;
    memcpy(mcn, m_MCN, 14);
    return true;
}

bool CCueBinFileDevice::GetISRC(int track, char isrc[13]) const
{
    for (int i = 0; i < m_nTracks; i++) {
        if (m_Tracks[i].track_number == track) {
            if (m_ISRC[i][0] == '\0')
                return false;
            memcpy(isrc, m_ISRC[i], 13);
            return true;
        }
    }
    return false;
}

int CCueBinFileDevice::OpenTrackFile(const char *filename, u64 *pSize)
{
    if (m_nFiles >= MaxFiles) {
        LOGERR("Too many files in cue sheet (max %d)", MaxFiles);
        return -1;
    }

    char path[768];
    snprintf(path, sizeof(path), "%s%s", m_DirPrefix, filename);

    FIL *f = new FIL();
    FRESULT result = f_open(f, path, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open track file: %s (error %d)", path, result);
        delete f;
        return -1;
    }

    int idx = m_nFiles++;
    m_Files[idx] = f;
    FatFsOptimizer::EnableFastSeek(f, &m_CLMTs[idx], 128, "CUE/BIN: ");
    *pSize = f_size(f);

    LOGNOTE("Opened track file %d: %s (%llu bytes)", idx, path, (unsigned long long)*pSize);
    return idx;
}

bool CCueBinFileDevice::ParseAndBuild()
{
    CUEParser parser(m_SourceCue);
    const CUETrackInfo *ti;

    u64 prevFileSize = 0;     // size of the file holding the previously returned track
    int parserFileIndex = -1; // CUEParser's file_index of the current file
    int curFile = -1;         // index into m_Files
    u64 curFileSize = 0;
    int curRun = -1;          // open file segment (vlen not yet final), or -1
    u64 vcursor = 0;
    int prevSession = 1;
    u32 lbaShift = 0;         // accumulated inter-session gap frames
    u32 prevSectorLen = 2352;
    u64 lastFo = 0;           // file_offset of the last track within the current file
    u32 lastSl = 2352;

    // Close the open run so that it ends at the given in-file byte offset
    auto closeRun = [&](u64 fileEndOffset) {
        if (curRun < 0)
            return;
        Segment &seg = m_Segments[curRun];
        seg.vlen = (fileEndOffset > seg.fileOffset) ? (fileEndOffset - seg.fileOffset) : 0;
        vcursor = seg.vstart + seg.vlen;
        curRun = -1;
    };

    auto addZeroSeg = [&](u64 bytes) -> bool {
        if (bytes == 0)
            return true;
        if (m_nSegments >= MaxSegments)
            return false;
        m_Segments[m_nSegments++] = {vcursor, bytes, -1, 0};
        vcursor += bytes;
        return true;
    };

    auto openRun = [&](int fileIdx, u64 fileOffset) -> bool {
        if (m_nSegments >= MaxSegments)
            return false;
        curRun = m_nSegments;
        m_Segments[m_nSegments++] = {vcursor, 0, fileIdx, fileOffset};
        return true;
    };

    while ((ti = parser.next_track(prevFileSize)) != nullptr) {
        if (m_nTracks >= MaxTracks) {
            LOGERR("Too many tracks in cue sheet (max %d)", MaxTracks);
            return false;
        }

        bool newFile = (ti->file_index != parserFileIndex);

        if (newFile)
            closeRun(ConsumedBytes(curFileSize, lastFo, lastSl));

        // Inter-session gap: not stored in any file, but it occupies LBAs
        // (and therefore virtual bytes) so that absolute addresses within
        // the later session's filesystem stay correct.
        while (ti->session > prevSession) {
            u32 gapFrames = SessionGapFrames(prevSession);
            if (!addZeroSeg((u64)gapFrames * prevSectorLen)) {
                LOGERR("Too many segments");
                return false;
            }
            lbaShift += gapFrames;
            prevSession++;
        }

        if (newFile) {
            curFile = OpenTrackFile(ti->filename, &curFileSize);
            if (curFile < 0)
                return false;
            parserFileIndex = ti->file_index;

            // An unstored pregap (PREGAP directive) becomes a zero-filled
            // region preceding the track's stored data.
            if (ti->unstored_pregap_length > 0) {
                if (!addZeroSeg((u64)ti->unstored_pregap_length * ti->sector_length))
                    return false;
            }
            if (!openRun(curFile, ti->file_offset)) {
                LOGERR("Too many segments");
                return false;
            }
        } else if (ti->unstored_pregap_length > 0) {
            // Unstored pregap between tracks of the same file: split the run
            closeRun(ti->file_offset);
            if (!addZeroSeg((u64)ti->unstored_pregap_length * ti->sector_length))
                return false;
            if (!openRun(curFile, ti->file_offset)) {
                LOGERR("Too many segments");
                return false;
            }
        }

        // Record the track with disc-absolute (gap-shifted) addresses
        m_Tracks[m_nTracks] = *ti;
        m_Tracks[m_nTracks].track_start += lbaShift;
        m_Tracks[m_nTracks].data_start += lbaShift;
        m_Tracks[m_nTracks].file_start += lbaShift;
        m_nTracks++;

        prevFileSize = curFileSize;
        prevSectorLen = ti->sector_length ? ti->sector_length : 2352;
        lastFo = ti->file_offset;
        lastSl = prevSectorLen;
    }

    if (m_nTracks == 0) {
        LOGERR("No tracks parsed from cue sheet");
        return false;
    }

    u64 consumed = ConsumedBytes(curFileSize, lastFo, lastSl);
    closeRun(consumed);

    m_vsize = vcursor;
    m_LeadoutLBA = m_Tracks[m_nTracks - 1].data_start + (u32)((consumed - lastFo) / lastSl);

    LOGNOTE("Built %d track(s), %d file(s), %d segment(s), %llu bytes, leadout LBA %u",
            m_nTracks, m_nFiles, m_nSegments, (unsigned long long)m_vsize, m_LeadoutLBA);

    return true;
}

bool CCueBinFileDevice::EmitNormalizedCue()
{
    // Single virtual FILE, absolute INDEX times, REM SESSION markers,
    // no PREGAP: the contract consumed by the gadget's CUEParser.
    size_t bufSize = 128 + (size_t)m_nTracks * 100;
    char *buf = new char[bufSize];
    char *p = buf;
    size_t remaining = bufSize;
    int session = 0;

    int len = snprintf(p, remaining, "FILE \"multibin\" BINARY\n");
    p += len;
    remaining -= len;

    for (int i = 0; i < m_nTracks && remaining > 100; i++) {
        const CUETrackInfo &t = m_Tracks[i];

        if (t.session != session) {
            session = t.session;
            len = snprintf(p, remaining, "REM SESSION %02d\n", session);
            p += len;
            remaining -= len;
        }

        len = snprintf(p, remaining, "  TRACK %02d %s\n", t.track_number, TrackModeString(t.track_mode));
        p += len;
        remaining -= len;

        if (t.track_start < t.data_start) {
            len = snprintf(p, remaining, "    INDEX 00 %02u:%02u:%02u\n",
                           t.track_start / (75 * 60), (t.track_start / 75) % 60, t.track_start % 75);
            p += len;
            remaining -= len;
        }

        len = snprintf(p, remaining, "    INDEX 01 %02u:%02u:%02u\n",
                       t.data_start / (75 * 60), (t.data_start / 75) % 60, t.data_start % 75);
        p += len;
        remaining -= len;
    }

    m_NormalizedCue = buf;
    LOGNOTE("Normalized cue sheet:\n%s", m_NormalizedCue);
    return true;
}

bool CCueBinFileDevice::LBAToFileOffset(u32 lba, int* pFileIdx, u64* pFileOffset) const
{
    // Find the track containing the LBA (pregap LBAs belong to the track
    // whose track_start they follow)
    const CUETrackInfo* track = nullptr;
    for (int i = 0; i < m_nTracks; i++) {
        u32 end = (i + 1 < m_nTracks) ? m_Tracks[i + 1].track_start : m_LeadoutLBA;
        if (lba >= m_Tracks[i].track_start && lba < end) {
            track = &m_Tracks[i];
            break;
        }
    }
    if (track == nullptr)
        return false;

    u64 v = CUEByteOffset(*track, lba);
    int si = FindSegment(v);
    if (si < 0 || m_Segments[si].fileIdx < 0)
        return false;

    *pFileIdx = m_Segments[si].fileIdx;
    *pFileOffset = m_Segments[si].fileOffset + (v - m_Segments[si].vstart);
    return true;
}

int CCueBinFileDevice::FindSegment(u64 vpos) const
{
    if (m_nSegments == 0)
        return -1;

    int i = m_LastSeg;
    if (i >= m_nSegments || vpos < m_Segments[i].vstart)
        i = 0;

    for (; i < m_nSegments; i++) {
        if (vpos >= m_Segments[i].vstart && vpos < m_Segments[i].vstart + m_Segments[i].vlen) {
            m_LastSeg = i;
            return i;
        }
    }
    return -1;
}

int CCueBinFileDevice::Read(void *pBuffer, size_t nCount)
{
    u8 *dest = static_cast<u8 *>(pBuffer);
    size_t total = 0;

    while (total < nCount && m_vpos < m_vsize) {
        int si = FindSegment(m_vpos);
        if (si < 0)
            break;

        const Segment &seg = m_Segments[si];
        u64 segRemain = seg.vstart + seg.vlen - m_vpos;
        size_t chunk = nCount - total;
        if ((u64)chunk > segRemain)
            chunk = segRemain;

        if (seg.fileIdx < 0) {
            memset(dest + total, 0, chunk);
        } else {
            FIL *f = m_Files[seg.fileIdx];
            u64 fpos = seg.fileOffset + (m_vpos - seg.vstart);
            if (f_tell(f) != fpos) {
                FRESULT result = f_lseek(f, fpos);
                if (result != FR_OK) {
                    LOGERR("Seek failed in file %d to %llu, err %d",
                           seg.fileIdx, (unsigned long long)fpos, result);
                    return total > 0 ? (int)total : -1;
                }
            }
            UINT got = 0;
            FRESULT result = f_read(f, dest + total, chunk, &got);
            if (result != FR_OK) {
                LOGERR("Read failed in file %d at %llu, err %d",
                       seg.fileIdx, (unsigned long long)fpos, result);
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

int CCueBinFileDevice::Write(const void *pBuffer, size_t nSize)
{
    // Read-only device
    return -1;
}

u64 CCueBinFileDevice::Tell() const
{
    return m_vpos;
}

u64 CCueBinFileDevice::Seek(u64 nOffset)
{
    m_vpos = nOffset;
    return nOffset;
}

u64 CCueBinFileDevice::GetSize(void) const
{
    return m_vsize;
}

const char *CCueBinFileDevice::GetCueSheet() const
{
    return m_NormalizedCue;
}

int CCueBinFileDevice::GetNumTracks() const
{
    return m_nTracks;
}

u32 CCueBinFileDevice::GetTrackStart(int track) const
{
    if (track < 0 || track >= m_nTracks)
        return 0;
    return m_Tracks[track].data_start;
}

u32 CCueBinFileDevice::GetTrackLength(int track) const
{
    if (track < 0 || track >= m_nTracks)
        return 0;
    u32 end = (track + 1 < m_nTracks) ? m_Tracks[track + 1].track_start : m_LeadoutLBA;
    return end - m_Tracks[track].data_start;
}

bool CCueBinFileDevice::IsAudioTrack(int track) const
{
    if (track < 0 || track >= m_nTracks)
        return false;
    return m_Tracks[track].track_mode == CUETrack_AUDIO;
}
