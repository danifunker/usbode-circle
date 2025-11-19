#include "mdsfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

LOGMODULE("CMdsFileDevice");

// For strlcpy
#include <circle/util.h>

// For ReadFileToString
#include "util.h"

//
// Given a path to an MDS file, this class will parse the MDS file,
// and generate a CUE sheet in memory. The CUE sheet will then be
// used to mount the MDF file.
//
CMdsFileDevice::CMdsFileDevice(const char* mdsPath) : m_cueSheet(nullptr), m_fileOpen(false) {
    char* mdsContent = nullptr;
    if (!ReadFileToString(mdsPath, &mdsContent)) {
        LOGERR("Failed to read MDS file %s", mdsPath);
        return;
    }

    // Find the MDF filename in the MDS content
    const char* mdf_filename_key = "Filename=";
    char* mdf_filename_start = strstr(mdsContent, mdf_filename_key);
    if (mdf_filename_start) {
        mdf_filename_start += strlen(mdf_filename_key);
        char* mdf_filename_end = strchr(mdf_filename_start, '\r');
        if (mdf_filename_end) {
            *mdf_filename_end = '\0';
        }

        char mdfPath[255];
        strlcpy(mdfPath, mdsPath, sizeof(mdfPath));
        char* mds_filename_in_path = strrchr(mdfPath, '/');
        if (mds_filename_in_path) {
            *(mds_filename_in_path + 1) = '\0';
            strlcat(mdfPath, mdf_filename_start, sizeof(mdfPath));
        }

        FRESULT res = f_open(&m_mdfFile, mdfPath, FA_READ);
        if (res == FR_OK) {
            m_fileOpen = true;

            // Generate CUE sheet
            size_t cueSheetLen = strlen("FILE \"\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n") + strlen(mdf_filename_start) + 1;
            m_cueSheet = new char[cueSheetLen];
            snprintf(m_cueSheet, cueSheetLen, "FILE \"%s\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", mdf_filename_start);
        } else {
            LOGERR("Failed to open MDF file %s", mdfPath);
        }
    }

    delete[] mdsContent;
}

CMdsFileDevice::~CMdsFileDevice() {
    if (m_fileOpen) {
        f_close(&m_mdfFile);
    }
    delete[] m_cueSheet;
}

int CMdsFileDevice::Read(void* pBuffer, size_t nSize) {
    if (!m_fileOpen) {
        return -1;
    }
    UINT bytesRead;
    FRESULT res = f_read(&m_mdfFile, pBuffer, nSize, &bytesRead);
    if (res != FR_OK) {
        return -1;
    }
    return bytesRead;
}

int CMdsFileDevice::Write(const void* pBuffer, size_t nSize) {
    return -1; // Not supported
}

u64 CMdsFileDevice::Seek(u64 ullOffset) {
    if (!m_fileOpen) {
        return -1;
    }
    FRESULT res = f_lseek(&m_mdfFile, ullOffset);
    if (res != FR_OK) {
        return -1;
    }
    return f_tell(&m_mdfFile);
}

u64 CMdsFileDevice::GetSize(void) const {
    if (!m_fileOpen) {
        return 0;
    }
    return f_size(&m_mdfFile);
}

MEDIA_TYPE CMdsFileDevice::GetMediaType() const {
    // TODO: Detect media type from MDS file
    return MEDIA_TYPE::CD;
}

const char* CMdsFileDevice::GetCueSheet() const {
    return m_cueSheet;
}
