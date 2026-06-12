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

bool hasCcdExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'c' &&
               tolower(ext[2]) == 'c' &&
               tolower(ext[3]) == 'd';
    }
    return false;
}

bool hasImgExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 4) {
        const char* ext = imageName + len - 4;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 'i' &&
               tolower(ext[2]) == 'm' &&
               tolower(ext[3]) == 'g';
    }
    return false;
}

bool hasToastExtension(const char* imageName) {
    size_t len = strlen(imageName);
    if (len >= 6) {
        const char* ext = imageName + len - 6;
        return tolower(ext[0]) == '.' &&
               tolower(ext[1]) == 't' &&
               tolower(ext[2]) == 'o' &&
               tolower(ext[3]) == 'a' &&
               tolower(ext[4]) == 's' &&
               tolower(ext[5]) == 't';
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
IImageDevice* loadMDSFileDevice(const char* imagePath) {
    LOGNOTE("Loading MDS image: %s", imagePath);

    MEDIA_TYPE mediaType = hasDvdHint(imagePath) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;

    // imagePath is already a full path like "1:/Games/game.mds"
    char fullPath[512];
    strncpy(fullPath, imagePath, sizeof(fullPath) - 1);
    fullPath[sizeof(fullPath) - 1] = '\0';

    // Read MDS file into memory
    char* mds_str = nullptr;
    if (!ReadFileToString(fullPath, &mds_str)) {
        LOGERR("Failed to read MDS file: %s", fullPath);
        return nullptr;
    }

    // Create MDS device
    CMDSFileDevice* mdsDevice = new CMDSFileDevice(fullPath, mds_str, mediaType);
    if (!mdsDevice->Init()) {
        LOGERR("Failed to initialize MDS device: %s", imagePath);
        delete mdsDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded MDS device: %s (has subchannels: %s)",
            imagePath,
            mdsDevice->HasSubchannelData() ? "yes" : "no");
    
    // Returns IMDSDevice*, which is an IImageDevice*
    return mdsDevice;
}

// ============================================================================
// CUE/BIN/ISO Plugin Loader
// ============================================================================
IImageDevice* loadCueBinIsoFileDevice(const char* imagePath) {
    LOGNOTE("Loading CUE/BIN/ISO image: %s", imagePath);

    MEDIA_TYPE mediaType = hasDvdHint(imagePath) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;

    // imagePath is already a full path like "1:/Games/game.iso"
    char fullPath[512];
    strncpy(fullPath, imagePath, sizeof(fullPath) - 1);
    fullPath[sizeof(fullPath) - 1] = '\0';

    // Handle BIN files - look for matching CUE
    if (hasBinExtension(fullPath)) {
        LOGNOTE("BIN file detected, looking for CUE file");
        change_extension_to_cue(fullPath);
    }

    // Handle CUE files: the device resolves and opens the bin file(s) the
    // cue sheet references (multi-bin images reference several).
    if (hasCueExtension(fullPath)) {
        LOGNOTE("Loading CUE sheet from: %s", fullPath);
        char* cue_str = nullptr;
        if (!ReadFileToString(fullPath, &cue_str)) {
            LOGERR("Failed to read CUE file: %s", fullPath);
            return nullptr;
        }

        CCueBinFileDevice* device = new CCueBinFileDevice(fullPath, cue_str, mediaType);
        delete[] cue_str; // device made its own copy

        if (!device->Init()) {
            LOGERR("Failed to initialize CUE/BIN device: %s", fullPath);
            delete device;
            return nullptr;
        }

        LOGNOTE("Successfully loaded CUE/BIN device: %s", imagePath);
        return device;
    }

    // ISO/TOAST: single data file, no cue sheet
    FIL* imageFile = new FIL();
    LOGNOTE("Opening data file: %s", fullPath);
    FRESULT result = f_open(imageFile, fullPath, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open data file for reading: %s (error %d)", fullPath, result);
        delete imageFile;
        return nullptr;
    }

    CCueBinFileDevice* device = new CCueBinFileDevice(imageFile, mediaType);
    if (!device->Init()) {
        LOGERR("Failed to initialize ISO device: %s", fullPath);
        delete device;
        return nullptr;
    }

    LOGNOTE("Successfully loaded ISO device: %s", imagePath);
    return device;
}

// ============================================================================
// CloneCD (CCD/IMG/SUB) Plugin Loader
// ============================================================================
IImageDevice* loadCCDFileDevice(const char* imagePath) {
    LOGNOTE("Loading CCD image: %s", imagePath);

    MEDIA_TYPE mediaType = hasDvdHint(imagePath) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;

    char fullPath[512];
    strncpy(fullPath, imagePath, sizeof(fullPath) - 1);
    fullPath[sizeof(fullPath) - 1] = '\0';

    // An .img path redirects to its sibling .ccd
    if (hasImgExtension(fullPath)) {
        size_t len = strlen(fullPath);
        fullPath[len - 3] = 'c';
        fullPath[len - 2] = 'c';
        fullPath[len - 1] = 'd';
    }

    char* ccd_str = nullptr;
    if (!ReadFileToString(fullPath, &ccd_str)) {
        LOGERR("Failed to read CCD file: %s", fullPath);
        return nullptr;
    }

    CCCDFileDevice* device = new CCCDFileDevice(fullPath, ccd_str, mediaType);
    delete[] ccd_str; // device made its own copy

    if (!device->Init()) {
        LOGERR("Failed to initialize CCD device: %s", fullPath);
        delete device;
        return nullptr;
    }

    LOGNOTE("Successfully loaded CCD device: %s (has subchannels: %s)",
            imagePath, device->HasSubchannelData() ? "yes" : "no");
    return device;
}

IImageDevice* loadCHDFileDevice(const char* imagePath) {
    LOGNOTE("Loading CHD image: %s", imagePath);

    MEDIA_TYPE mediaType = hasDvdHint(imagePath) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;

    // imagePath is already a full path like "1:/Games/game.chd"
    char fullPath[512];
    strncpy(fullPath, imagePath, sizeof(fullPath) - 1);
    fullPath[sizeof(fullPath) - 1] = '\0';
    
    // Create CHD device
    CCHDFileDevice* chdDevice = new CCHDFileDevice(fullPath, mediaType);
    if (!chdDevice->Init()) {
        LOGERR("Failed to initialize CHD device: %s", imagePath);
        delete chdDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded CHD device: %s (has subchannels: %s)",
            imagePath,
            chdDevice->HasSubchannelData() ? "yes" : "no");
    
    return chdDevice;
}

boolean FatFsOptimizer::EnableFastSeek(FIL* pFile, DWORD** ppCLMT, size_t clmtSize, const char* logPrefix) {
    if (!pFile || !ppCLMT) {
        return false;
    }
    
    // Allocate CLMT array
    *ppCLMT = new DWORD[clmtSize];
    if (!*ppCLMT) {
        LOGERR("%sFast seek: Failed to allocate CLMT", logPrefix);
        return false;
    }
    
    // Set up CLMT in file handle
    pFile->cltbl = *ppCLMT;
    (*ppCLMT)[0] = clmtSize;
    
    // Create the cluster link map
    FRESULT result = f_lseek(pFile, CREATE_LINKMAP);
    
    if (result == FR_OK) {
        f_lseek(pFile, 0);
        LOGNOTE("%sFast seek enabled, using %u CLMT entries", logPrefix, (*ppCLMT)[0]);
        return true;
    } 
    else if (result == FR_NOT_ENOUGH_CORE) {
        LOGERR("%sFast seek: CLMT too small, need %u entries (have %zu)", 
                logPrefix, (*ppCLMT)[0], clmtSize);
    } 
    else {
        LOGERR("%sFast seek: Creation failed with error %d", logPrefix, result);
    }
    
    // Cleanup on failure
    delete[] *ppCLMT;
    *ppCLMT = nullptr;
    pFile->cltbl = nullptr;
    return false;
}

void FatFsOptimizer::DisableFastSeek(DWORD** ppCLMT) {
    if (ppCLMT && *ppCLMT) {
        delete[] *ppCLMT;
        *ppCLMT = nullptr;
    }
}

// ============================================================================
// Main Entry Point - Plugin Selection
// ============================================================================
IImageDevice* loadImageDevice(const char* imagePath) {
    // imagePath is a full path like "1:/Games/game.iso"
    LOGNOTE("loadImageDevice called for: %s", imagePath);

    if (hasMdsExtension(imagePath)) {
        LOGNOTE("Detected MDS format - using MDS plugin");
        return loadMDSFileDevice(imagePath);
    }
    else if (hasChdExtension(imagePath)) {
        LOGNOTE("Detected CHD format - using CHD plugin");
        return loadCHDFileDevice(imagePath);
    }
    else if (hasCcdExtension(imagePath) || hasImgExtension(imagePath)) {
        LOGNOTE("Detected CCD format - using CCD plugin");
        return loadCCDFileDevice(imagePath);
    }
    else if (hasCueExtension(imagePath) || hasBinExtension(imagePath) || hasIsoExtension(imagePath) || hasToastExtension(imagePath)) {
        LOGNOTE("Detected CUE/BIN/ISO/TOAST format - using CUE plugin");
        return loadCueBinIsoFileDevice(imagePath);
    }
    else {
        LOGERR("Unknown file format: %s", imagePath);
        return nullptr;
    }
}