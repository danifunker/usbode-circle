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

    m_pCacheBuffer = new u8[CacheSize];
}

CCueBinFileDevice::~CCueBinFileDevice(void) {
    // NEW: Use shared Fast Seek helper
    FatFsOptimizer::DisableFastSeek(&m_pCLMT);

    delete[] m_pCacheBuffer;
    m_pCacheBuffer = nullptr;

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
    if (m_pCacheBuffer && nSize <= m_nCacheLen &&
        m_nLogicalPos >= m_nCacheStart &&
        m_nLogicalPos + nSize <= m_nCacheStart + m_nCacheLen) {
        memcpy(pBuffer, m_pCacheBuffer + (m_nLogicalPos - m_nCacheStart), nSize);
        m_nLogicalPos += nSize;
        return nSize;
    }

    // Requests larger than the cache go straight through and don't disturb it.
    if (!m_pCacheBuffer || nSize > CacheSize) {
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

    // Cache miss: read a larger window than requested, so the next
    // sequential read (the common case) is served from the cache.
    FRESULT result = f_lseek(m_pFile, m_nLogicalPos);
    if (result != FR_OK) {
        LOGERR("Seek to offset %llu failed, err %d", m_nLogicalPos, result);
        return -1;
    }

    UINT nBytesRead = 0;
    result = f_read(m_pFile, m_pCacheBuffer, CacheSize, &nBytesRead);
    if (result != FR_OK) {
        LOGERR("Failed to read %d bytes into memory, err %d", (int)CacheSize, result);
        m_nCacheLen = 0;
        return -1;
    }

    m_nCacheStart = m_nLogicalPos;
    m_nCacheLen = nBytesRead;

    size_t nServe = ((size_t)nBytesRead < nSize) ? (size_t)nBytesRead : nSize;
    memcpy(pBuffer, m_pCacheBuffer, nServe);
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

    // Just record the position; Read() lazily seeks the underlying file
    // only on a cache miss, so a Seek() into an already-cached region
    // costs nothing.
    m_nLogicalPos = nOffset;
    return nOffset;
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