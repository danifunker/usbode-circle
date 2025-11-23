//
// Utils for disc image manipulation
//
// This is the entry point for listing and mounting disc images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
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
#include "util.h"
#include "cuebinfile.h"
#include "mdsfile.h"
#include "chdfile.h"
#include "ccdfile.h"

LOGMODULE("discimage-util");

char tolower(char c) {
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

bool hasExtension(const char* imageName, const char* ext) {
    size_t len = strlen(imageName);
    size_t extLen = strlen(ext);
    if (len >= extLen) {
        const char* p = imageName + len - extLen;
        for (size_t i = 0; i < extLen; i++) {
            if (tolower(p[i]) != tolower(ext[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void change_extension_to_bin(char* fullPath) {
    size_t len = strlen(fullPath);
    if (len >= 3) {
        fullPath[len - 3] = 'b';
        fullPath[len - 2] = 'i';
        fullPath[len - 1] = 'n';
    }
}

void change_extension_to_cue(char* fullPath) {
    size_t len = strlen(fullPath);
    if (len >= 3) {
        fullPath[len - 3] = 'c';
        fullPath[len - 2] = 'u';
        fullPath[len - 1] = 'e';
    }
}

bool hasDvdHint(const char* imageName) {
    const char* p = imageName;
    while (*p) {
        // Look for a dot followed by 'd', 'v', 'd', dot (case-insensitive)
        if (tolower(p[0]) == '.' &&
            tolower(p[1]) == 'd' &&
            tolower(p[2]) == 'v' &&
            tolower(p[3]) == 'd' &&
            tolower(p[4]) == '.') {
            return true;
        }
        ++p;
    }
    return false;
}

bool ReadFileToString(const char* fullPath, char** out_str) {
    if (!out_str) return false;  // safeguard

    FIL* file = new FIL();
    FRESULT result = f_open(file, fullPath, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open file for reading: %s", fullPath);
        delete file;
        return false;
    }

    DWORD file_size = f_size(file);
    char* buffer = new char[file_size + 1];
    if (!buffer) {
        f_close(file);
        delete file;
        return false;
    }

    UINT bytes_read = 0;
    result = f_read(file, buffer, file_size, &bytes_read);
    f_close(file);
    delete file;

    if (result != FR_OK || bytes_read != file_size) {
        delete[] buffer;
        return false;
    }

    buffer[file_size] = '\0';  // null-terminate
    *out_str = buffer;
    return true;
}

// ============================================================================
// MDS Plugin Loader
// ============================================================================
IImageDevice* loadMDSFileDevice(const char* imageName) {
    LOGNOTE("Loading MDS image: %s", imageName);
    
    MEDIA_TYPE mediaType = hasDvdHint(imageName) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;
    
    // Construct full path for MDS file
    char fullPath[255];
    snprintf(fullPath, sizeof(fullPath), "1:/%s", imageName);

    // Read MDS file into memory
    char* mds_str = nullptr;
    if (!ReadFileToString(fullPath, &mds_str)) {
        LOGERR("Failed to read MDS file: %s", fullPath);
        return nullptr;
    }

    // Create MDS device
    CMDSFileDevice* mdsDevice = new CMDSFileDevice(fullPath, mds_str, mediaType);
    if (!mdsDevice->Init()) {
        LOGERR("Failed to initialize MDS device: %s", imageName);
        delete mdsDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded MDS device: %s (has subchannels: %s)", 
            imageName, 
            mdsDevice->HasSubchannelData() ? "yes" : "no");
    
    // Returns IMDSDevice*, which is an IImageDevice*
    return mdsDevice;
}

// ============================================================================
// CUE/BIN/ISO Plugin Loader
// ============================================================================
IImageDevice* loadCueBinIsoFileDevice(const char* imageName) {
    LOGNOTE("Loading CUE/BIN/ISO image: %s", imageName);
    
    MEDIA_TYPE mediaType = hasDvdHint(imageName) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;
    
    char data_path[255];
    snprintf(data_path, sizeof(data_path), "1:/%s", imageName);

    FIL* imageFile = new FIL();
    char* cue_str = nullptr;

    if (hasExtension(imageName, ".bin")) {
        LOGNOTE("BIN file detected, looking for CUE file");

        char cue_path[255];
        snprintf(cue_path, sizeof(cue_path), "1:/%s", imageName);
        change_extension_to_cue(cue_path);

        if (!ReadFileToString(cue_path, &cue_str)) {
            LOGWARN("Could not find or read matching CUE file: %s", cue_path);
        } else {
            LOGNOTE("Loaded CUE sheet");
        }
    } else if (hasExtension(imageName, ".cue")) {
        LOGNOTE("Loading CUE sheet from: %s", data_path);
        if (!ReadFileToString(data_path, &cue_str)) {
            LOGERR("Failed to read CUE file: %s", data_path);
            delete imageFile;
            return nullptr;
        }
        LOGNOTE("Loaded CUE sheet");

        // Switch to BIN file for data
        change_extension_to_bin(data_path);
    }

    // Open the data file (BIN or ISO)
    LOGNOTE("Opening data file: %s", data_path);
    FRESULT result = f_open(imageFile, data_path, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open data file for reading: %s (error %d)", data_path, result);
        delete imageFile;
        if (cue_str) delete[] cue_str;
        return nullptr;
    }
    LOGNOTE("Opened data file successfully");

    // Create device
    CCueBinFileDevice* device = new CCueBinFileDevice(imageFile, cue_str, mediaType);

    // CCueBinFileDevice takes ownership of cue_str if provided

    LOGNOTE("Successfully loaded CUE/BIN/ISO device: %s", imageName);
    
    return device;
}

IImageDevice* loadCHDFileDevice(const char* imageName) {
    LOGNOTE("Loading CHD image: %s", imageName);
    
    MEDIA_TYPE mediaType = hasDvdHint(imageName) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;
    
    // Construct full path
    char fullPath[255];
    snprintf(fullPath, sizeof(fullPath), "1:/%s", imageName);
    
    // Create CHD device
    CCHDFileDevice* chdDevice = new CCHDFileDevice(fullPath, mediaType);
    if (!chdDevice->Init()) {
        LOGERR("Failed to initialize CHD device: %s", imageName);
        delete chdDevice;
        return nullptr;
    }
    
    LOGNOTE("Successfully loaded CHD device: %s (has subchannels: %s)", 
            imageName, 
            chdDevice->HasSubchannelData() ? "yes" : "no");
    
    return chdDevice;
}

IImageDevice* loadCcdFileDevice(const char* imageName) {
    LOGNOTE("Loading CCD image: %s", imageName);

    CCcdFileDevice* ccdDevice = new CCcdFileDevice(imageName);
    if (!ccdDevice->Init()) {
        LOGERR("Failed to initialize CCD device: %s", imageName);
        delete ccdDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded CCD device: %s", imageName);
    return ccdDevice;
}

IImageDevice* loadImageDevice(const char* imageName) {
    LOGNOTE("loadImageDevice called for: %s", imageName);
    
    if (hasExtension(imageName, ".mds")) {
        LOGNOTE("Detected MDS format - using MDS plugin");
        return loadMDSFileDevice(imageName);
    }
    else if (hasExtension(imageName, ".chd")) {
        LOGNOTE("Detected CHD format - using CHD plugin");
        return loadCHDFileDevice(imageName);
    }
    else if (hasExtension(imageName, ".ccd")) {
        LOGNOTE("Detected CCD format - using CCD plugin");
        return loadCcdFileDevice(imageName);
    }
    else if (hasExtension(imageName, ".cue") || hasExtension(imageName, ".bin") || hasExtension(imageName, ".iso")) {
        LOGNOTE("Detected CUE/BIN/ISO format - using CUE plugin");
        return loadCueBinIsoFileDevice(imageName);
    }
    else {
        LOGERR("Unknown file format: %s", imageName);
        return nullptr;
    }
}
