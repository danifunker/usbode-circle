#include "mdsfile.h"
#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../mdsparser/mdsparser.h"

LOGMODULE("CMDSFileDevice");

CMDSFileDevice::CMDSFileDevice(const char* mds_filename, char *mds_str, MEDIA_TYPE mediaType) :
    m_mds_filename(mds_filename),
    m_mds_str(mds_str),
    m_mediaType(mediaType)
{
}

bool CMDSFileDevice::Init() {
    m_parser = new MDSParser(m_mds_str);
    if (!m_parser->isValid()) {
        LOGERR("Invalid MDS file");
        return false;
    }

    // Open MDF file
    const char* mdf_filename = m_parser->getMDFilename();
    LOGNOTE("MDF filename from parser: %s", mdf_filename);
    char mdf_path[255];

    if (strcmp(mdf_filename, "*.mdf") == 0) {
        // Handle wildcard filename
        const char* extension = strrchr(m_mds_filename, '.');
        if (extension) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s.mdf", (int)(extension - m_mds_filename), m_mds_filename);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s.mdf", m_mds_filename);
        }
    } else {
        const char* last_slash = strrchr(m_mds_filename, '/');
        if (last_slash) {
            snprintf(mdf_path, sizeof(mdf_path), "%.*s%s", (int)(last_slash - m_mds_filename + 1), m_mds_filename, mdf_filename);
        } else {
            snprintf(mdf_path, sizeof(mdf_path), "%s", mdf_filename);
        }
    }

    LOGNOTE("Attempting to open MDF file at: %s", mdf_path);
    m_pFile = new FIL();
    FRESULT result = f_open(m_pFile, mdf_path, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open MDF file for reading (FatFs error %d)", result);
        delete m_pFile;
        m_pFile = nullptr;

        LOGNOTE("Scanning for similar files...");
        DIR dir;
        FILINFO fno;
        if (f_opendir(&dir, "1:/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
                if (!(fno.fattrib & AM_DIR)) {
                    LOGNOTE("Found file: %s", fno.fname);
                }
            }
            f_closedir(&dir);
        }
        return false;
    }

    // Generate CUE sheet
    char cue_buffer[4096];
    char* cue_ptr = cue_buffer;
    int remaining = sizeof(cue_buffer);

    int len = snprintf(cue_ptr, remaining, "FILE \"%s\" BINARY\n", mdf_filename);
    cue_ptr += len;
    remaining -= len;

    for (int i = 0; i < m_parser->getNumSessions(); i++) {
        MDS_SessionBlock* session = m_parser->getSession(i);
        for (int j = 0; j < session->num_all_blocks; j++) {
            MDS_TrackBlock* track = m_parser->getTrack(i, j);
            MDS_TrackExtraBlock* extra = m_parser->getTrackExtra(i, j);

            const char* mode_str = (track->mode == 0x00) ? "AUDIO" : "MODE1/2352";
            len = snprintf(cue_ptr, remaining, "  TRACK %02d %s\n", track->tno, mode_str);
            cue_ptr += len;
            remaining -= len;

            if (extra->pregap > 0) {
                int minutes = extra->pregap / (75 * 60);
                int seconds = (extra->pregap / 75) % 60;
                int frames = extra->pregap % 75;
                len = snprintf(cue_ptr, remaining, "    PREGAP %02d:%02d:%02d\n", minutes, seconds, frames);
                cue_ptr += len;
                remaining -= len;
            }

            int minutes = track->start_sector / (75 * 60);
            int seconds = (track->start_sector / 75) % 60;
            int frames = track->start_sector % 75;
            len = snprintf(cue_ptr, remaining, "    INDEX 01 %02d:%02d:%02d\n", minutes, seconds, frames);
            cue_ptr += len;
            remaining -= len;
        }
    }

    m_cue_sheet = new char[strlen(cue_buffer) + 1];
    strcpy(m_cue_sheet, cue_buffer);

    return true;
}

CMDSFileDevice::~CMDSFileDevice(void) {
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

int CMDSFileDevice::Read(void *pBuffer, size_t nSize) {
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

int CMDSFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    return -1;
}

u64 CMDSFileDevice::Tell() const {
    if (!m_pFile) {
        LOGERR("Tell !m_pFile");
        return static_cast<u64>(-1);
    }

    return f_tell(m_pFile);
}

u64 CMDSFileDevice::Seek(u64 nOffset) {
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

u64 CMDSFileDevice::GetSize(void) const {
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

const char *CMDSFileDevice::GetCueSheet() const {
    return m_cue_sheet;
}
