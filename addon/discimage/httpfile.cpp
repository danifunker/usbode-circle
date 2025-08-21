//
// This device makes a remotely hosted file look like a local file to Circle
// using HTTP range requests. Performance is improved by using persistent
// connections
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
#include "httpfile.h"

#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>

LOGMODULE("HTTPDevice");

// The URLs in the argument must be http only not https and may contain
// either a ip address or hostname
HTTPFileDevice::HTTPFileDevice(char *pFileURL, char *char pCueURL) {
    if (pCueURL != nullptr) {
	// TODO
        // If we were given a cue sheet URL, request it via HTTP and store it in
	// m_cue_sheet
    } else {
        // If we were not given a cue sheet
        // make a copy of our default cue sheet
        size_t len = strlen(default_cue_sheet);
        m_cue_str = new char[len + 1];
        strcpy(m_cue_str, default_cue_sheet);
        m_FileType = FileType::ISO;
    }
}

HTTPFileDevice::~HTTPFileDevice(void) {
    if (m_cue_str != nullptr)
        delete[] m_cue_str;
}

int HTTPFileDevice::Read(void *pBuffer, size_t nSize) {
    //TODO Request bytes from the HTTP server using range requests
    //The start range should be according to the logical file pointer
    //either from the start position (0) or as a result of an incremented
    //file pointer based on the last read or Seek
    return nBytesRead;
}

int HTTPFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    // Not supported
    return -1;
}

u64 HTTPFileDevice::Tell() const {
    //TODO return current file pointer
    return f_tell(m_pFile);
}

u64 HTTPFileDevice::Seek(u64 nOffset) {
    //TODO store the new filter pointer for the next read
    return nOffset;
}

u64 HTTPFileDevice::GetSize(void) const {
    // Return the size of the file on the HTTP server. If we don't
    // yet know it, get it using a Range: 0-1 request
    return size;
}

const char *HTTPFileDevice::GetCueSheet() const {
    return m_cue_str;
}

