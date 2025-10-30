//
// Utils for cue/bin manipulation
//
// This is the entry point for listing and mounting CD images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
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
#include "util.h"

LOGMODULE("cueparser-util");

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
        LOGERR("Cannot open file for reading");
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

ICueDevice* loadCueBinFileDevice(const char* imageName) {
    MEDIA_TYPE mediaType = hasDvdHint(imageName) ? MEDIA_TYPE::DVD : MEDIA_TYPE::CD;
    
    // Construct full path
    char fullPath[255];
    snprintf(fullPath, sizeof(fullPath), "1:/%s", imageName);

    FIL* imageFile = new FIL();
    char* cue_str = nullptr;
    bool isIsoFile = false;

    // Is this a bin?
    if (hasBinExtension(fullPath)) {
        //LOGNOTE("This is a bin file, changing to cue");
        change_extension_to_cue(fullPath);
    }
    // Is this an ISO?
    else if (hasIsoExtension(fullPath)) {
        LOGNOTE("This is an ISO file, will generate default CUE sheet");
        isIsoFile = true;
        // Keep the .iso extension for opening the file
    }
    // Is this a cue?
    else if (hasCueExtension(fullPath)) {
        // Load the cue
        //LOGNOTE("This is a cue file, loading cue");
        if (!ReadFileToString(fullPath, &cue_str)) {
            return nullptr;
        }
        LOGNOTE("Loaded cue %s", cue_str);

        // Load a bin file with the same name
        change_extension_to_bin(fullPath);
        //LOGNOTE("Changed to bin %s", fullPath);
    }

    // Load the image
    //LOGNOTE("Opening image file %s", fullPath);
    FRESULT Result = f_open(imageFile, fullPath, FA_READ);
    if (Result != FR_OK) {
        LOGERR("Cannot open image file for reading");
        delete imageFile;
        return nullptr;
    }
    LOGNOTE("Opened image file %s", fullPath);

    // NEW: Generate default CUE sheet for ISO files
    if (isIsoFile && cue_str == nullptr) {
        // Use imageName directly (it's just the filename, no path)
        // Allocate buffer for CUE sheet
        cue_str = new char[512];
        
        // Generate minimal CUE sheet for MODE1/2048 (standard ISO format)
        snprintf(cue_str, 512,
            "FILE \"%s\" BINARY\n"
            "  TRACK 01 MODE1/2048\n"
            "    INDEX 01 00:00:00\n",
            imageName);
        
        LOGNOTE("Generated default CUE sheet for ISO:\n%s", cue_str);
    }

    // Create our device
    ICueDevice* ccueBinFileDevice = new CCueBinFileDevice(imageFile, cue_str, mediaType);

    // Cleanup
    if (cue_str != nullptr)
        delete[] cue_str;

    return ccueBinFileDevice;
}