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

LOGMODULE("discimage-util");

char tolower(char c) {
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

bool hasCueExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'c' &&
               tolower(ext[2]) == 'u' &&
               tolower(ext[3]) == 'e';
    }
    return false;
}

bool hasMdsExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'm' &&
               tolower(ext[2]) == 'd' &&
               tolower(ext[3]) == 's';
    }
    return false;
}

bool hasBinExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'b' &&
               tolower(ext[2]) == 'i' &&
               tolower(ext[3]) == 'n';
    }
    return false;
}

bool hasIsoExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'i' &&
               tolower(ext[2]) == 's' &&
               tolower(ext[3]) == 'o';
    }
    return false;
}

bool hasChdExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'c' &&
               tolower(ext[2]) == 'h' &&
               tolower(ext[3]) == 'd';
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
    
    // Construct full path
    char fullPath[255];
    snprintf(fullPath, sizeof(fullPath), "1:/%s", imageName);

    FIL* imageFile = new FIL();
    char* cue_str = nullptr;

    // Handle BIN files - look for matching CUE
    if (hasBinExtension(fullPath)) {
        LOGNOTE("BIN file detected, looking for CUE file");
        change_extension_to_cue(fullPath);
    }

    // Handle CUE files
    if (hasCueExtension(fullPath)) {
        LOGNOTE("Loading CUE sheet from: %s", fullPath);
        if (!ReadFileToString(fullPath, &cue_str)) {
            LOGERR("Failed to read CUE file: %s", fullPath);
            delete imageFile;
            return nullptr;
        }
        LOGNOTE("Loaded CUE sheet");

        // Switch to BIN file for data
        change_extension_to_bin(fullPath);
    }

    // Open the data file (BIN or ISO)
    LOGNOTE("Opening data file: %s", fullPath);
    FRESULT result = f_open(imageFile, fullPath, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open data file for reading: %s (error %d)", fullPath, result);
        delete imageFile;
        if (cue_str) delete[] cue_str;
        return nullptr;
    }
    LOGNOTE("Opened data file successfully");

    // Create device
    CCueBinFileDevice* device = new CCueBinFileDevice(imageFile, cue_str, mediaType);

    // Cleanup - CCueBinFileDevice takes ownership of cue_str if provided
    if (cue_str != nullptr)
        delete[] cue_str;

    LOGNOTE("Successfully loaded CUE/BIN/ISO device: %s", imageName);
    
    // Returns ICueDevice*, which is an IImageDevice*
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
        return nullptr;  // This nullptr is causing the crash
    }
    
    LOGNOTE("Successfully loaded CHD device: %s (has subchannels: %s)", 
            imageName, 
            chdDevice->HasSubchannelData() ? "yes" : "no");
    
    return chdDevice;
}

// ============================================================================
// Main Entry Point - Plugin Selection
// ============================================================================
IImageDevice* loadImageDevice(const char* imageName) {
    LOGNOTE("loadImageDevice called for: %s", imageName);
    
    if (hasMdsExtension(imageName)) {
        LOGNOTE("Detected MDS format - using MDS plugin");
        return loadMDSFileDevice(imageName);
    } 
    else if (hasChdExtension(imageName)) {
        LOGNOTE("Detected CHD format - using CHD plugin");
        return loadCHDFileDevice(imageName);
    }
    else if (hasCueExtension(imageName) || hasBinExtension(imageName) || hasIsoExtension(imageName)) {
        LOGNOTE("Detected CUE/BIN/ISO format - using CUE plugin");
        return loadCueBinIsoFileDevice(imageName);
    }
    else {
        LOGERR("Unknown file format: %s", imageName);
        return nullptr;
    }
}