//
// A CDevice for cue/bin files
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
#include <string.h>

LOGMODULE("CCueBinFileDevice");

CCueBinFileDevice::CCueBinFileDevice(FIL *pFile, char *cue_str, MEDIA_TYPE mediaType) : m_mediaType(mediaType) {
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
}

CCueBinFileDevice::~CCueBinFileDevice(void) {
    f_close(m_pFile);
    delete m_pFile;
    m_pFile = nullptr;

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

    UINT nBytesRead = 0;
    FRESULT result = f_read(m_pFile, pBuffer, nSize, &nBytesRead);
    if (result != FR_OK) {
        LOGERR("Failed to read %d bytes into memory, err %d", nSize, result);
        return -1;
    }
    return nBytesRead;
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

    return f_tell(m_pFile);
}

u64 CCueBinFileDevice::Seek(u64 nOffset) {
    if (!m_pFile) {
        LOGERR("Seek !m_pFile");
        return static_cast<u64>(-1);
    }

    // Don't seek if we're already there
    if (Tell() == nOffset)
	    return nOffset;

    FRESULT result = f_lseek(m_pFile, nOffset);
    if (result != FR_OK) {
        LOGERR("Seek to offset %llu is not ok, err %d", nOffset, result);
        return 0;
    }
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
    
    if (!m_cue_str) {
        // No CUE sheet - assume single data track
        m_numTracks = 1;
        m_tracks[0].track_number = 1;
        m_tracks[0].track_start = 0;
        m_tracks[0].sector_length = 2048;
        m_tracks[0].track_mode = CUETrack_MODE1_2048;
        m_tracks[0].file_offset = 0;
        m_tracks[0].track_length = GetSize() / 2048;
        m_tracksParsed = true;
        return;
    }
    
    // Use CUEParser to parse the sheet
    CUEParser parser(m_cue_str);
    const CUETrackInfo* trackInfo;
    
    m_numTracks = 0;
    while ((trackInfo = parser.next_track()) != nullptr && m_numTracks < MAX_TRACKS) {
        m_tracks[m_numTracks].track_number = trackInfo->track_number;
        m_tracks[m_numTracks].track_start = trackInfo->data_start;
        m_tracks[m_numTracks].sector_length = trackInfo->sector_length;
        m_tracks[m_numTracks].track_mode = trackInfo->track_mode;
        m_tracks[m_numTracks].file_offset = trackInfo->file_offset;
        m_numTracks++;
    }
    
    // Calculate track lengths (distance to next track or end of file)
    u64 fileSize = GetSize();
    for (int i = 0; i < m_numTracks; i++) {
        if (i < m_numTracks - 1) {
            // Length is distance to next track
            u32 sectorsToNext = m_tracks[i + 1].track_start - m_tracks[i].track_start;
            m_tracks[i].track_length = sectorsToNext;
        } else {
            // Last track - calculate from file size
            u64 remainingBytes = fileSize - m_tracks[i].file_offset;
            m_tracks[i].track_length = remainingBytes / m_tracks[i].sector_length;
        }
    }
    
    m_tracksParsed = true;
}

int CCueBinFileDevice::GetNumTracks() const {
    ParseCueSheet();
    return m_numTracks;
}

u32 CCueBinFileDevice::GetTrackStart(int track) const {
    ParseCueSheet();
    if (track < 1 || track > m_numTracks) return 0;
    return m_tracks[track - 1].track_start;
}

u32 CCueBinFileDevice::GetTrackLength(int track) const {
    ParseCueSheet();
    if (track < 1 || track > m_numTracks) return 0;
    return m_tracks[track - 1].track_length;
}

bool CCueBinFileDevice::IsAudioTrack(int track) const {
    ParseCueSheet();
    if (track < 1 || track > m_numTracks) return false;
    return m_tracks[track - 1].track_mode == CUETrack_AUDIO;
}