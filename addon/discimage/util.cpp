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
#include <cueparser/cueutil.h>
#include "mdsfile.h"
// The host test suite has a build without libchdr; everything else keeps CHD.
#ifndef USBODE_NO_CHD
#include "chdfile.h"
#endif

#include <stdarg.h>

LOGMODULE("discimage-util");

// Reason the last load failed. Every loader returns nullptr, so without this the
// reason only ever reached the log.
static char s_LastImageLoadError[192] = {0};

const char* GetLastImageLoadError() {
    return s_LastImageLoadError;
}

static void ClearImageLoadError() {
    s_LastImageLoadError[0] = '\0';
}

static void SetImageLoadError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(s_LastImageLoadError, sizeof(s_LastImageLoadError), format, args);
    va_end(args);
}

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

// Read a whole file into a NUL-terminated heap buffer. out_size, when given,
// receives the file's length: a cue sheet is text and its length is implied
// by the terminator, but an MDS is binary and the parser needs the real
// length to range check the offsets stored inside it.
bool ReadFileToString(const char* fullPath, char** out_str, size_t* out_size = nullptr) {
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
    if (out_size) {
        *out_size = file_size;
    }
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
    size_t mds_size = 0;
    if (!ReadFileToString(fullPath, &mds_str, &mds_size)) {
        LOGERR("Failed to read MDS file: %s", fullPath);
        SetImageLoadError("Could not read the .mds file. It may be unreadable or too large.");
        return nullptr;
    }

    // Create MDS device
    CMDSFileDevice* mdsDevice = new CMDSFileDevice(fullPath, mds_str, mds_size, mediaType);
    if (!mdsDevice->Init()) {
        LOGERR("Failed to initialize MDS device: %s", imagePath);
        SetImageLoadError("Not a valid Alcohol 120%% image, or its .mdf data file is missing.");
        delete mdsDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded MDS device: %s (has subchannels: %s)",
            imagePath,
            mdsDevice->HasSubchannelData() ? "yes" : "no");
    
    // Returns IMDSDevice*, which is an IImageDevice*
    return mdsDevice;
}

// FatFs runs with FF_FS_RPATH 2, so a ".." component would leave the cue's own
// directory. "." is harmless and common, and both slashes separate components.
static bool isSafeCueFileName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    if (name[0] == '/' || name[0] == '\\') {
        return false;
    }
    size_t start = 0;
    for (size_t i = 0;; i++) {
        if (name[i] != '/' && name[i] != '\\' && name[i] != '\0') {
            continue;
        }
        if (i - start == 2 && name[start] == '.' && name[start + 1] == '.') {
            return false;
        }
        if (name[i] == '\0') {
            break;
        }
        start = i + 1;
    }
    return true;
}

// A FILE line names a path relative to the directory holding the cue sheet.
static void resolveSiblingPath(const char* cuePath, const char* name,
                               char* out, size_t outSize) {
    // Circle's util.h has strchr but not strrchr, so find the last one here.
    size_t dirLen = 0;
    for (size_t i = 0; cuePath[i] != '\0'; i++) {
        if (cuePath[i] == '/') {
            dirLen = i + 1;
        }
    }
    if (dirLen >= outSize) {
        dirLen = 0;
    }
    memcpy(out, cuePath, dirLen);
    strncpy(out + dirLen, name, outSize - dirLen - 1);
    out[outSize - 1] = '\0';
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

    FIL* imageFile = new FIL();
    char* cue_str = nullptr;

    // Handle BIN files - look for matching CUE
    if (hasBinExtension(fullPath)) {
        LOGNOTE("BIN file detected, looking for CUE file");
        change_extension_to_cue(fullPath);
    }

    // Handle CUE files
    char cuePath[512];
    cuePath[0] = '\0';
    if (hasCueExtension(fullPath)) {
        LOGNOTE("Loading CUE sheet from: %s", fullPath);
        if (!ReadFileToString(fullPath, &cue_str)) {
            LOGERR("Failed to read CUE file: %s", fullPath);
            SetImageLoadError("Could not read the cue sheet for this image.");
            delete imageFile;
            return nullptr;
        }
        LOGNOTE("Loaded CUE sheet");

        strncpy(cuePath, fullPath, sizeof(cuePath) - 1);
        cuePath[sizeof(cuePath) - 1] = '\0';

        // Switch to BIN file for data
        change_extension_to_bin(fullPath);
    }

    // Single-FILE cues retain the same-stem BIN fallback.
    int nCueFiles = (cue_str != nullptr) ? CueCountFiles(cue_str) : 0;
    bool bSplitRip = (nCueFiles > 1 && cuePath[0] != '\0');
    if (bSplitRip) {
        char name[CUE_MAX_FILENAME + 1];
        if (!CueGetFileName(cue_str, 0, name, sizeof(name))) {
            LOGERR("Cue sheet names %d files but the first is unreadable", nCueFiles);
            SetImageLoadError("This image's cue sheet lists a data file it does not name.");
            delete imageFile;
            delete[] cue_str;
            return nullptr;
        }
        if (!isSafeCueFileName(name)) {
            LOGERR("Cue sheet names a data file outside its own directory: %s", name);
            SetImageLoadError("This image's cue sheet names an unsafe file path: %s", name);
            delete imageFile;
            delete[] cue_str;
            return nullptr;
        }
        resolveSiblingPath(cuePath, name, fullPath, sizeof(fullPath));
        LOGNOTE("Split rip: cue names %d data files", nCueFiles);
    }

    // Open the data file (BIN or ISO)
    LOGNOTE("Opening data file: %s", fullPath);
    FRESULT result = f_open(imageFile, fullPath, FA_READ);
    if (result != FR_OK) {
        LOGERR("Cannot open data file for reading: %s (error %d)", fullPath, result);
        // "Missing" sends the user looking for a file that may be sitting right
        // there: FR_DENIED, FR_INVALID_NAME and a failing card all land here too.
        if (result == FR_NO_FILE || result == FR_NO_PATH) {
            SetImageLoadError("The data file this image needs is missing: %s", fullPath);
        } else {
            SetImageLoadError("The data file this image needs would not open: %s (FatFs error %d)",
                              fullPath, (int)result);
        }
        delete imageFile;
        if (cue_str) delete[] cue_str;
        return nullptr;
    }
    LOGNOTE("Opened data file successfully");

    // Reject empty files before they collapse later file offsets.
    if (bSplitRip && f_size(imageFile) == 0) {
        LOGERR("Split-rip data file is empty: %s", fullPath);
        SetImageLoadError("This image's data file %s is empty (0 bytes).", fullPath);
        f_close(imageFile);
        delete imageFile;
        delete[] cue_str;
        return nullptr;
    }

    // Create device
    CCueBinFileDevice* device = new CCueBinFileDevice(imageFile, cue_str, mediaType);

    for (int i = 1; bSplitRip && i < nCueFiles; i++) {
        char name[CUE_MAX_FILENAME + 1];
        char binPath[512];
        if (!CueGetFileName(cue_str, i, name, sizeof(name))) {
            LOGERR("Cue sheet names %d files but entry %d is unreadable", nCueFiles, i);
            SetImageLoadError("This image's cue sheet lists a data file it does not name.");
            delete device;
            if (cue_str != nullptr) delete[] cue_str;
            return nullptr;
        }
        if (!isSafeCueFileName(name)) {
            LOGERR("Cue sheet names a data file outside its own directory: %s", name);
            SetImageLoadError("This image's cue sheet names an unsafe file path: %s", name);
            delete device;
            if (cue_str != nullptr) delete[] cue_str;
            return nullptr;
        }
        resolveSiblingPath(cuePath, name, binPath, sizeof(binPath));

        FIL* extraFile = new FIL();
        FRESULT extraResult = f_open(extraFile, binPath, FA_READ);
        if (extraResult != FR_OK) {
            LOGERR("Cannot open split-rip data file: %s (error %d)", binPath, extraResult);
            SetImageLoadError("This image needs the file %s, which would not open.", name);
            delete extraFile;
            delete device;
            if (cue_str != nullptr) delete[] cue_str;
            return nullptr;
        }

        // Reject empty files before they collapse later file offsets.
        if (f_size(extraFile) == 0) {
            LOGERR("Split-rip data file is empty: %s", binPath);
            SetImageLoadError("This image's data file %s is empty (0 bytes).", name);
            f_close(extraFile);
            delete extraFile;
            delete device;
            if (cue_str != nullptr) delete[] cue_str;
            return nullptr;
        }

        if (!device->AddDataFile(extraFile)) {
            LOGERR("Cannot adopt split-rip data file: %s", binPath);
            SetImageLoadError("This image's data files do not form a usable disc layout at %s.",
                              name);
            f_close(extraFile);
            delete extraFile;
            delete device;
            if (cue_str != nullptr) delete[] cue_str;
            return nullptr;
        }
    }

    // Cleanup - CCueBinFileDevice takes ownership of cue_str if provided
    if (cue_str != nullptr)
        delete[] cue_str;

    LOGNOTE("Successfully loaded CUE/BIN/ISO device: %s", imagePath);
    
    // Returns ICueDevice*, which is an IImageDevice*
    return device;
}

IImageDevice* loadCHDFileDevice(const char* imagePath) {
#ifdef USBODE_NO_CHD
    LOGERR("CHD support is not compiled in: %s", imagePath);
    SetImageLoadError("This build cannot open CHD images.");
    return nullptr;
#else
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
        SetImageLoadError("Not a valid CHD image, or it uses an unsupported compression.");
        delete chdDevice;
        return nullptr;
    }

    LOGNOTE("Successfully loaded CHD device: %s (has subchannels: %s)",
            imagePath,
            chdDevice->HasSubchannelData() ? "yes" : "no");

    return chdDevice;
#endif
}

boolean FatFsOptimizer::EnableFastSeek(FIL* pFile, DWORD** ppCLMT, size_t clmtSize, const char* logPrefix) {
    if (!pFile || !ppCLMT) {
        return false;
    }

    // A heavily fragmented image file needs more CLMT entries than the
    // default. On FR_NOT_ENOUGH_CORE FatFs writes the required entry count
    // into element 0, so retry once with that size. Without fast seek, the
    // read-ahead cache's per-window f_lseek() calls degrade to FAT chain
    // walks, which on a large fragmented file stalls playback badly enough
    // to look like a system freeze (reported on 3.0.5+).
    static constexpr size_t MaxCLMTEntries = 65536; // 256 KB ceiling

    for (int attempt = 0; attempt < 2; attempt++) {
        // Allocate CLMT array
        *ppCLMT = new DWORD[clmtSize];
        if (!*ppCLMT) {
            LOGERR("%sFast seek: Failed to allocate CLMT (%zu entries)", logPrefix, clmtSize);
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

        size_t needed = (*ppCLMT)[0];

        delete[] *ppCLMT;
        *ppCLMT = nullptr;
        pFile->cltbl = nullptr;

        if (result != FR_NOT_ENOUGH_CORE) {
            LOGERR("%sFast seek: Creation failed with error %d", logPrefix, result);
            return false;
        }

        if (attempt > 0 || needed <= clmtSize || needed > MaxCLMTEntries) {
            LOGERR("%sFast seek: CLMT too small, need %zu entries (limit %zu) - "
                   "file is heavily fragmented, fast seek disabled",
                   logPrefix, needed, MaxCLMTEntries);
            return false;
        }

        LOGNOTE("%sFast seek: retrying with %zu CLMT entries (fragmented file)",
                logPrefix, needed);
        clmtSize = needed;
    }

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

    ClearImageLoadError();

    if (hasMdsExtension(imagePath)) {
        LOGNOTE("Detected MDS format - using MDS plugin");
        return loadMDSFileDevice(imagePath);
    }
    else if (hasChdExtension(imagePath)) {
        LOGNOTE("Detected CHD format - using CHD plugin");
        return loadCHDFileDevice(imagePath);
    }
    else if (hasCueExtension(imagePath) || hasBinExtension(imagePath) || hasIsoExtension(imagePath) || hasToastExtension(imagePath)) {
        LOGNOTE("Detected CUE/BIN/ISO/TOAST format - using CUE plugin");
        return loadCueBinIsoFileDevice(imagePath);
    }
    else {
        LOGERR("Unknown file format: %s", imagePath);
        SetImageLoadError("Unsupported file type. USBODE mounts .iso, .cue/.bin, .chd, .mds and .toast images.");
        return nullptr;
    }
}