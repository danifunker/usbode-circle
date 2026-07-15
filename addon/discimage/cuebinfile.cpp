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
    : m_mediaType(mediaType),
      m_pCLMT(nullptr)
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
        FatFsOptimizer::EnableFastSeek(m_pFile, &m_pCLMT, 256, "BIN/ISO: ");
        m_nLogicalPos = f_tell(m_pFile);
    }

    for (int i = 0; i < NumCacheWindows; i++) {
        m_CacheWindows[i].pBuffer = new u8[CacheSize];
    }
}

CCueBinFileDevice::~CCueBinFileDevice(void) {
    // NEW: Use shared Fast Seek helper
    FatFsOptimizer::DisableFastSeek(&m_pCLMT);

    for (int i = 0; i < NumCacheWindows; i++) {
        delete[] m_CacheWindows[i].pBuffer;
        m_CacheWindows[i].pBuffer = nullptr;
    }

    if (m_pFile) {
        f_close(m_pFile);
        delete m_pFile;
        m_pFile = nullptr;
    }

    if (m_cue_str != nullptr) {
        delete[] m_cue_str;
        m_cue_str = nullptr;
    }
}

int CCueBinFileDevice::Read(void *pBuffer, size_t nSize) {
    if (!m_pFile) {
        LOGERR("Read !m_pFile");
        return -1;
    }

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

    // Requests larger than the cache go straight through and don't disturb it.
    if (!m_CacheWindows[0].pBuffer || nSize > CacheSize) {
        FRESULT result = f_lseek(m_pFile, m_nLogicalPos);
        if (result != FR_OK) {
            LOGERR("Seek to offset %llu failed, err %d", m_nLogicalPos, result);
            return -1;
        }

        UINT nBytesRead = 0;
        result = f_read(m_pFile, pBuffer, nSize, &nBytesRead);
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

    FRESULT result = f_lseek(m_pFile, m_nLogicalPos);
    if (result != FR_OK) {
        LOGERR("Seek to offset %llu failed, err %d", m_nLogicalPos, result);
        return -1;
    }

    UINT nBytesRead = 0;
    result = f_read(m_pFile, pVictim->pBuffer, CacheSize, &nBytesRead);
    if (result != FR_OK) {
        LOGERR("Failed to read %d bytes into memory, err %d", (int)CacheSize, result);
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
    return CueLBAToByteOffset(m_cue_str, lba);
}

u64 CCueBinFileDevice::GetSize(void) const {
    if (!m_pFile) {
        LOGERR("GetSize !m_pFile");
        return 0;
    }

    u64 size = f_size(m_pFile);
    if (size < 0) {
        LOGERR("GetSize f_size < 0");
        return 0;
    }

    return size;
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