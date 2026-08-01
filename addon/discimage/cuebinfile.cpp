//
// A CDevice for cue/bin files
//
// Copyright (C) 2025 Ian Cass
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
#include <cueparser/cueutil.h>
#include <stdlib.h>
#include <string.h>
#include <circle/timer.h>

LOGMODULE("CCueBinFileDevice");

CCueBinFileDevice::CCueBinFileDevice(FIL *pFile, char *cue_str, MEDIA_TYPE mediaType)
    : m_mediaType(mediaType)
{
    m_pFile = pFile;
    if (cue_str != nullptr) {
        // If we were given a cue sheet
        // copy it and own it
        size_t len = strlen(cue_str);
        m_cue_str = new char[len + 1];
        strcpy(m_cue_str, cue_str);
        m_FileType = FileType::CUEBIN;
    } else {
        // If we were not given a cue sheet
        // make a copy of our default cue sheet
        size_t len = strlen(default_cue_sheet);
        m_cue_str = new char[len + 1];
        strcpy(m_cue_str, default_cue_sheet);
        m_FileType = FileType::ISO;
    }
    
    // NEW: Use shared Fast Seek helper
    if (m_pFile) {
        m_Files[0].pFile = m_pFile;
        m_Files[0].nBase = 0;
        m_Files[0].nSize = f_size(m_pFile);
        m_FileSizes[0] = m_Files[0].nSize;
        m_nFileCount = 1;
        m_nVirtualSize = m_Files[0].nSize;
        FatFsOptimizer::EnableFastSeek(m_pFile, &m_Files[0].pCLMT, 256, "BIN/ISO: ");
        m_nLogicalPos = f_tell(m_pFile);
    }

    for (int i = 0; i < NumCacheWindows; i++) {
        m_CacheWindows[i].pBuffer = new u8[CacheSize];
    }
}

CCueBinFileDevice::~CCueBinFileDevice(void) {
    for (int i = 0; i < NumCacheWindows; i++) {
        delete[] m_CacheWindows[i].pBuffer;
        m_CacheWindows[i].pBuffer = nullptr;
    }

    for (int i = 0; i < m_nFileCount; i++) {
        DataFile &file = m_Files[i];
        // Clear FatFs' pointer before freeing its CLMT.
        if (file.pFile) {
            file.pFile->cltbl = nullptr;
        }
        FatFsOptimizer::DisableFastSeek(&file.pCLMT);
        if (file.pFile) {
            f_close(file.pFile);
            delete file.pFile;
            file.pFile = nullptr;
        }
    }
    m_nFileCount = 0;
    m_pFile = nullptr;

    if (m_cue_str != nullptr) {
        delete[] m_cue_str;
        m_cue_str = nullptr;
    }
}

bool CCueBinFileDevice::AddDataFile(FIL *pFile) {
    if (pFile == nullptr || m_nFileCount == 0 || m_nFileCount >= MaxDataFiles) {
        return false;
    }

    DataFile &next = m_Files[m_nFileCount];
    next.pFile = pFile;
    next.nSize = f_size(pFile);
    m_FileSizes[m_nFileCount] = next.nSize;
    m_nFileCount++;

    // On rejection the file was never adopted: the caller still owns and closes
    // it, so drop it here or the destructor closes it a second time.
    if (!RebuildLayout()) {
        next.pFile = nullptr;
        next.nSize = 0;
        m_FileSizes[--m_nFileCount] = 0;
        RebuildLayout();
        return false;
    }

    // Without a link map every seek walks the FAT chain and stalls playback.
    if (!FatFsOptimizer::EnableFastSeek(pFile, &next.pCLMT, 256, "BIN/ISO split: ")) {
        LOGWARN("Fast seek unavailable for split file %d", m_nFileCount - 1);
    }
    return true;
}

namespace {

// Where one file's stored frames begin and end on the disc.
struct FileFrameExtent {
    u32 nStartLBA = 0;
    u32 nEndLBA = 0;
    u32 nSectorLength = 0;
    bool bValid = false;
};

}  // namespace

// A boundary this cannot represent must not become a contiguous one: abutting
// the files is exactly the aliasing that unbacked frames have to avoid.
bool CCueBinFileDevice::RebuildLayout() {
    if (m_nFileCount <= 1) {
        m_nSparseCount = 0;
        m_Files[0].nBase = 0;
        m_nVirtualSize = (m_nFileCount == 1) ? m_Files[0].nSize : 0;
        return true;
    }

    if (m_cue_str == nullptr) {
        LOGERR("Split image has %d files and no cue sheet", m_nFileCount);
        return false;
    }

    FileFrameExtent extents[MaxDataFiles];
    CUEParser parser(m_cue_str);
    const CUETrackInfo *track = nullptr;
    int prevFileIndex = 0;
    while ((track = parser.next_track(prevFileIndex >= 1 && prevFileIndex <= m_nFileCount
                                          ? m_FileSizes[prevFileIndex - 1]
                                          : 0)) != nullptr) {
        prevFileIndex = track->file_index;
        int f = track->file_index - 1;
        if (f < 0 || f >= m_nFileCount) {
            continue;  // a file the cue names but the loader has not opened yet
        }
        if (track->sector_length == 0) {
            LOGERR("File %d track %d has no sector length", f, track->track_number);
            return false;
        }

        u32 headFrames = (u32)(track->file_offset / track->sector_length);
        FileFrameExtent &ext = extents[f];
        if (!ext.bValid) {
            if (track->data_start < headFrames) {
                LOGERR("File %d starts at frame %lu, under its offset of %lu frames",
                       f, (unsigned long)track->data_start, (unsigned long)headFrames);
                return false;
            }
            ext.nStartLBA = track->data_start - headFrames;
            ext.bValid = true;
        }

        // The last track of a file owns its tail, so this keeps overwriting.
        ext.nSectorLength = track->sector_length;
        const u64 size = m_FileSizes[f];
        ext.nEndLBA = (size > track->file_offset)
                          ? track->data_start +
                                (u32)((size - track->file_offset) / track->sector_length)
                          : track->data_start;
    }

    // Staged, so a rejected boundary leaves the previous layout in place.
    u64 bases[MaxDataFiles];
    SparseRange holes[MaxDataFiles];
    int holeCount = 0;
    u64 base = 0;

    for (int i = 0; i < m_nFileCount; i++) {
        if (!extents[i].bValid) {
            LOGERR("File %d is open but no cue track places it", i);
            return false;
        }
        if (base > (u64)-1 - m_Files[i].nSize) {
            LOGERR("File %d overflows the address space at base %llu", i, base);
            return false;
        }
        bases[i] = base;
        base += m_Files[i].nSize;

        if (i + 1 >= m_nFileCount || extents[i + 1].nStartLBA <= extents[i].nEndLBA) {
            continue;  // last file, contiguous, or a cue that overlaps its files
        }

        u32 frames = extents[i + 1].nStartLBA - extents[i].nEndLBA;
        if (frames > MaxGapFrames || extents[i].nSectorLength == 0) {
            LOGERR("Unbacked gap of %lu frames before file %d cannot be represented",
                   (unsigned long)frames, i + 1);
            return false;
        }

        SparseRange &hole = holes[holeCount++];
        hole.nStartLBA = extents[i].nEndLBA;
        hole.nEndLBA = extents[i + 1].nStartLBA;
        hole.nSectorLength = extents[i].nSectorLength;
        hole.nLength = (u64)frames * hole.nSectorLength;
        hole.nBase = base;
        if (base > (u64)-1 - hole.nLength) {
            LOGERR("Gap before file %d overflows the address space", i + 1);
            return false;
        }
        base += hole.nLength;
    }

    for (int i = 0; i < m_nFileCount; i++) {
        m_Files[i].nBase = bases[i];
    }
    for (int i = 0; i < holeCount; i++) {
        m_Sparse[i] = holes[i];
    }
    m_nSparseCount = holeCount;
    m_nVirtualSize = base;
    return true;
}

int CCueBinFileDevice::FileIndexForOffset(u64 nOffset) const {
    for (int i = 0; i < m_nFileCount; i++) {
        if (nOffset >= m_Files[i].nBase && nOffset < m_Files[i].nBase + m_Files[i].nSize) {
            return i;
        }
    }
    return -1;
}

u64 CCueBinFileDevice::SparseBytesAt(u64 nOffset) const {
    for (int i = 0; i < m_nSparseCount; i++) {
        const SparseRange &hole = m_Sparse[i];
        if (nOffset >= hole.nBase && nOffset < hole.nBase + hole.nLength) {
            return hole.nBase + hole.nLength - nOffset;
        }
    }
    return 0;
}

int CCueBinFileDevice::Read(void *pBuffer, size_t nSize) {
    if (m_nFileCount == 0) {
        LOGERR("Read !m_pFile");
        return -1;
    }

    // Continue reads across BIN boundaries.
    u8 *pOut = static_cast<u8 *>(pBuffer);
    size_t nDone = 0;
    while (nDone < nSize) {
        int nRead = ReadWithinFile(pOut + nDone, nSize - nDone);
        if (nRead < 0) {
            return (nDone > 0) ? (int)nDone : -1;
        }
        if (nRead == 0) {
            break;  // end of the image
        }
        nDone += (size_t)nRead;
    }

    return (int)nDone;
}

int CCueBinFileDevice::ReadWithinFile(void *pBuffer, size_t nSize) {
    // Cache hit: entirely served from RAM, no SD card access.
    for (int i = 0; i < NumCacheWindows; i++) {
        CacheWindow &win = m_CacheWindows[i];
        if (win.pBuffer && nSize <= win.nLen &&
            m_nLogicalPos >= win.nStart &&
            m_nLogicalPos + nSize <= win.nStart + win.nLen) {
            memcpy(pBuffer, win.pBuffer + (m_nLogicalPos - win.nStart), nSize);
            m_nLogicalPos += nSize;
            win.nLastUse = ++m_nCacheUseCounter;
            return nSize;
        }
    }

    int nFile = FileIndexForOffset(m_nLogicalPos);
    if (nFile < 0) {
        // In a hole no .bin is opened, seeked or cached: the frames are
        // digital silence, which is what a single-.bin rip stores there.
        u64 nHole = SparseBytesAt(m_nLogicalPos);
        if (nHole > 0) {
            if (nSize > nHole) {
                nSize = (size_t)nHole;
            }
            memset(pBuffer, 0, nSize);
            m_nLogicalPos += nSize;
            return (int)nSize;
        }
        if (m_nLogicalPos == GetSize()) {
            return 0;
        }
        LOGERR("Read at offset %llu past end of image", m_nLogicalPos);
        return -1;
    }
    FIL *pDataFile = m_Files[nFile].pFile;
    u64 nInFile = m_nLogicalPos - m_Files[nFile].nBase;
    u64 nAvail = m_Files[nFile].nSize - nInFile;
    if (nSize > nAvail) {
        nSize = (size_t)nAvail;
    }

    // Requests larger than the cache go straight through and don't disturb it.
    if (!m_CacheWindows[0].pBuffer || nSize > CacheSize) {
        FRESULT result = f_lseek(pDataFile, nInFile);
        if (result != FR_OK) {
            LOGERR("Seek to offset %llu failed, err %d", nInFile, result);
            return -1;
        }

        UINT nBytesRead = 0;
        result = f_read(pDataFile, pBuffer, nSize, &nBytesRead);
        if (result != FR_OK) {
            LOGERR("Failed to read %d bytes into memory, err %d", nSize, result);
            return -1;
        }
        m_nLogicalPos += nBytesRead;
        return nBytesRead;
    }

    // Cache miss: refill a window with a larger read than requested, so
    // the next sequential read of this stream (the common case) is served
    // from the cache. Prefer the window this stream just ran off the end
    // of - plain LRU would pick the other stream's window here, since our
    // own was touched most recently - and fall back to least recently
    // used, so the window the other stream is running in is left alone.
    CacheWindow *pVictim = nullptr;
    for (int i = 0; i < NumCacheWindows; i++) {
        CacheWindow &win = m_CacheWindows[i];
        if (win.pBuffer && win.nLen > 0 && win.nStart + win.nLen == m_nLogicalPos) {
            pVictim = &win;
            break;
        }
    }
    if (pVictim == nullptr) {
        for (int i = 0; i < NumCacheWindows; i++) {
            CacheWindow &win = m_CacheWindows[i];
            if (win.pBuffer && (!pVictim || win.nLastUse < pVictim->nLastUse)) {
                pVictim = &win;
            }
        }
    }

    FRESULT result = f_lseek(pDataFile, nInFile);
    if (result != FR_OK) {
        LOGERR("Seek to offset %llu failed, err %d", nInFile, result);
        return -1;
    }

    size_t nFill = (nAvail < CacheSize) ? (size_t)nAvail : CacheSize;
    UINT nBytesRead = 0;
    result = f_read(pDataFile, pVictim->pBuffer, nFill, &nBytesRead);
    if (result != FR_OK) {
        LOGERR("Failed to read %d bytes into memory, err %d", (int)nFill, result);
        pVictim->nLen = 0;
        return -1;
    }

    pVictim->nStart = m_nLogicalPos;
    pVictim->nLen = nBytesRead;
    pVictim->nLastUse = ++m_nCacheUseCounter;

    size_t nServe = ((size_t)nBytesRead < nSize) ? (size_t)nBytesRead : nSize;
    memcpy(pBuffer, pVictim->pBuffer, nServe);
    m_nLogicalPos += nServe;
    return nServe;
}

int CCueBinFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    return -1;
}

u64 CCueBinFileDevice::Tell() const {
    if (!m_pFile) {
        LOGERR("Tell !m_pFile");
        return static_cast<u64>(-1);
    }

    return m_nLogicalPos;
}

u64 CCueBinFileDevice::Seek(u64 nOffset) {
    if (!m_pFile) {
        LOGERR("Seek !m_pFile");
        return static_cast<u64>(-1);
    }

    // Reject seeks beyond the end of the image: a position past EOF can
    // only come from a wrong LBA-to-byte translation, and failing here
    // (callers handle it) beats stalling in short reads later.
    if (nOffset > GetSize()) {
        LOGERR("Seek to offset %llu beyond image size %llu", nOffset, GetSize());
        return static_cast<u64>(-1);
    }

    // Just record the position; Read() lazily seeks the underlying file
    // only on a cache miss, so a Seek() into an already-cached region
    // costs nothing.
    m_nLogicalPos = nOffset;
    return nOffset;
}

u64 CCueBinFileDevice::GetByteOffsetForLBA(u32 lba) const {
    // The Seek() space of this device is the raw BIN file, where each
    // track's stored sector size can differ (mixed-mode images). Translate
    // through the cue sheet; falls back to lba * 2352 for trackless cues.
    if (m_nFileCount <= 1) {
        return CueLBAToByteOffset(m_cue_str, lba);
    }

    // A frame no .bin stores answers inside its hole, so the read that follows
    // sees zeros rather than the next file's first bytes.
    for (int i = 0; i < m_nSparseCount; i++) {
        const SparseRange &hole = m_Sparse[i];
        if (lba >= hole.nStartLBA && lba < hole.nEndLBA) {
            return hole.nBase + (u64)(lba - hole.nStartLBA) * hole.nSectorLength;
        }
    }

    // The sheet needs the earlier files' sizes, and answers relative to one file.
    u64 sizes[MaxDataFiles];
    for (int i = 0; i < m_nFileCount; i++) {
        sizes[i] = m_Files[i].nSize;
    }

    CueFileLocation loc;
    if (!CueResolveLBA(m_cue_str, lba, sizes, m_nFileCount, &loc) ||
        loc.file_index < 0 || loc.file_index >= m_nFileCount) {
        // A single-file offset would address the wrong .bin, so fail the Seek().
        return static_cast<u64>(-1);
    }

    return m_Files[loc.file_index].nBase + loc.offset;
}

u64 CCueBinFileDevice::GetSize(void) const {
    if (m_nFileCount == 0) {
        LOGERR("GetSize !m_pFile");
        return 0;
    }

    return m_nVirtualSize;
}

const char *CCueBinFileDevice::GetCueSheet() const {
    return m_cue_str;
}

// Add to cuebinfile.cpp

void CCueBinFileDevice::ParseCueSheet() const {
    if (m_tracksParsed) return;
    
    // TODO: Implement proper CUE sheet parser
    // For now, simple default implementation
    m_numTracks = 1;
    m_tracksParsed = true;
}

int CCueBinFileDevice::GetNumTracks() const {
    ParseCueSheet();
    return m_numTracks;
}

u32 CCueBinFileDevice::GetTrackStart(int track) const {
    ParseCueSheet();
    if (track == 0) return 0;
    return 0; // TODO: Parse from CUE
}

u32 CCueBinFileDevice::GetTrackLength(int track) const {
    ParseCueSheet();
    // Simple calculation for single data track
    return GetSize() / 2048;
}

bool CCueBinFileDevice::IsAudioTrack(int track) const {
    ParseCueSheet();
    return false; // TODO: Parse from CUE
}