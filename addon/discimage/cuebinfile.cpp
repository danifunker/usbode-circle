#include "cuebinfile.h"

#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>

LOGMODULE("CCueBinFileDevice");

CCueBinFileDevice::CCueBinFileDevice(FIL *pFile, char *cue_str) {
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
    if (m_cue_str != nullptr)
        delete[] m_cue_str;
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

u64 CCueBinFileDevice::Seek(u64 nOffset) {
    if (!m_pFile) {
        LOGERR("Seek !m_pFile");
        return -1;
    }

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

