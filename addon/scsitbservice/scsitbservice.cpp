//
// A SCSCI Toolbox Service
//
// This is the entry point for listing and mounting CD images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
//
// Copyright (C) 2025 Ian Cass
// Copyright (C) 2025 Dani Sarfati
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
#include <circle/logger.h>
#include "scsitbservice.h"
#include <circle/sched/scheduler.h>
#include <cstdlib>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <discimage/cuebinfile.h>
#include <discimage/cuedevice.h>
#include <discimage/util.h>

LOGMODULE("scsitbservice");

static size_t my_strnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen; i++) {
        if (s[i] == '\0') break;
    }
    return i;
}

static bool iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

int compareFileEntries(const void* a, const void* b) {
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;
    return strcasecmp(fa->name, fb->name);
}

// Sort directories first, then files alphabetically
int compareFileEntriesDirectoriesFirst(const void* a, const void* b) {
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;

    // Directories come first
    if (fa->isDirectory && !fb->isDirectory) return -1;
    if (!fa->isDirectory && fb->isDirectory) return 1;

    // Within same type, sort alphabetically (case-insensitive)
    return strcasecmp(fa->name, fb->name);
}

SCSITBService *SCSITBService::s_pThis = 0;

SCSITBService::SCSITBService()
{
    LOGNOTE("SCSITBService::SCSITBService() called");

    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    cdromservice = static_cast<CDROMService*>(CScheduler::Get()->GetTask("cdromservice"));
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
    assert(cdromservice != nullptr && "Failed to get cdromservice");

    m_FileEntries = new FileEntry[MAX_FILES]();
    m_CurrentImagePath[0] = '\0';  // Initialize empty path

    bool ok = RefreshCache();
    assert(ok && "Failed to refresh SCSITBService on construction");
    SetName("scsitbservice");
}

SCSITBService::~SCSITBService() {
    delete[] m_FileEntries;
    m_FileEntries = nullptr;
}

size_t SCSITBService::GetCount() const {
    return m_FileCount;
}

const char* SCSITBService::GetName(size_t index) const {
    if (index >= m_FileCount)
        return nullptr;
    return m_FileEntries[index].name;
}

DWORD SCSITBService::GetSize(size_t index) const {
    if (index >= m_FileCount)
        return 0;
    return m_FileEntries[index].size;
}

FileEntry* SCSITBService::begin() { return m_FileEntries; }
FileEntry* SCSITBService::end() { return m_FileEntries + m_FileCount; }
const FileEntry* SCSITBService::GetFileEntry(size_t index) const {
    if (index >= m_FileCount)
        return nullptr;
    return &m_FileEntries[index];
}

size_t SCSITBService::GetCurrentCD() {
	return current_cd;
}

bool SCSITBService::IsDirectory(size_t index) const {
    if (index >= m_FileCount)
        return false;
    return m_FileEntries[index].isDirectory;
}

const char* SCSITBService::GetCurrentCDPath() const {
    return m_CurrentImagePath;
}

const char* SCSITBService::GetCurrentCDFolder() const {
    // Return folder portion without "1:/" prefix
    // e.g., "1:/Games/RPG/game.iso" -> "Games/RPG/"
    if (m_CurrentImagePath[0] == '\0')
        return "";

    // Skip "1:/" prefix
    const char* path = m_CurrentImagePath;
    if (strncmp(path, "1:/", 3) == 0)
        path += 3;

    // Find last slash to extract folder portion
    const char* lastSlash = strrchr(path, '/');
    if (lastSlash == nullptr || lastSlash == path)
        return "";  // No folder (root level)

    // Return static buffer with folder portion
    static char folderBuf[MAX_PATH_LEN];
    size_t folderLen = lastSlash - path;
    if (folderLen >= sizeof(folderBuf))
        folderLen = sizeof(folderBuf) - 1;
    memcpy(folderBuf, path, folderLen);
    folderBuf[folderLen] = '/';
    folderBuf[folderLen + 1] = '\0';
    return folderBuf;
}

void SCSITBService::GetFullPath(size_t index, char* outPath, size_t maxLen, const char* basePath) const {
    if (index >= m_FileCount || outPath == nullptr || maxLen == 0) {
        if (outPath && maxLen > 0)
            outPath[0] = '\0';
        return;
    }

    // Construct: "1:/" + basePath + name
    if (basePath == nullptr || basePath[0] == '\0') {
        snprintf(outPath, maxLen, "1:/%s", m_FileEntries[index].name);
    } else {
        // Ensure basePath doesn't have leading slash
        while (*basePath == '/')
            basePath++;
        snprintf(outPath, maxLen, "1:/%s%s", basePath, m_FileEntries[index].name);
    }
}

bool SCSITBService::SetNextCD(size_t cd) {
    //TODO bounds checking
    next_cd = cd;
    return true;
}

const char* SCSITBService::GetCurrentCDName() {
	return GetName(GetCurrentCD());
}

bool SCSITBService::SetNextCDByName(const char* file_name) {

	int index = 0;
        for (const FileEntry* it = begin(); it != end(); ++it, ++index) {
		//LOGNOTE("SCSITBService::SetNextCDByName testing %s", it->name);
                if (strcmp(file_name, it->name) == 0) {
                        return SetNextCD(index);
                }
        }

	return false;
}

bool SCSITBService::SetNextCDByPath(const char* fullPath) {
    LOGNOTE("SCSITBService::SetNextCDByPath called with: %s", fullPath);

    if (fullPath == nullptr || fullPath[0] == '\0')
        return false;

    // Store the path - will be used to load the image
    strncpy(m_CurrentImagePath, fullPath, sizeof(m_CurrentImagePath) - 1);
    m_CurrentImagePath[sizeof(m_CurrentImagePath) - 1] = '\0';

    // We don't actually look up the index in the current cache
    // because the file might be in a different folder than what's cached.
    // Instead, mark next_cd with a special value and handle in Run()
    // For now, we use -2 to indicate "use m_CurrentImagePath directly"
    next_cd = -2;

    return true;
}


// Internal helper to scan a directory for images and folders
bool SCSITBService::RefreshCacheInternal(const char* fullPath) {
    LOGNOTE("SCSITBService::RefreshCacheInternal() scanning: %s", fullPath);

    m_FileCount = 0;

    DIR dir;
    FRESULT fr = f_opendir(&dir, fullPath);
    if (fr != FR_OK) {
        LOGERR("SCSITBService::RefreshCacheInternal() failed to open directory: %s", fullPath);
        return false;
    }

    FILINFO fno;
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;

        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0)
            continue;

        // Exclude hidden files/folders (starting with .)
        if (fno.fname[0] == '.')
            continue;

        // Exclude Mac cache files
        if (strncmp(fno.fname, "._", 2) == 0)
            continue;

        // Exclude Windows/Linux system folders
        if (strcasecmp(fno.fname, "System Volume Information") == 0 ||
            strcasecmp(fno.fname, "$RECYCLE.BIN") == 0 ||
            strcasecmp(fno.fname, "RECYCLER") == 0 ||
            strcasecmp(fno.fname, "lost+found") == 0)
            continue;

        if (m_FileCount >= MAX_FILES)
            break;

        // Check if this is a directory
        if (fno.fattrib & AM_DIR) {
            size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
            memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
            m_FileEntries[m_FileCount].name[len] = '\0';
            m_FileEntries[m_FileCount].size = 0;
            m_FileEntries[m_FileCount].isDirectory = true;
            m_FileCount++;
            continue;
        }

        // Check for image files
        const char* ext = strrchr(fno.fname, '.');
        if (ext != nullptr) {
            if (iequals(ext, ".iso") || iequals(ext, ".bin") || iequals(ext, ".mds") || iequals(ext, ".chd")) {
                size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
                memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
                m_FileEntries[m_FileCount].name[len] = '\0';
                m_FileEntries[m_FileCount].size = fno.fsize;
                m_FileEntries[m_FileCount].isDirectory = false;
                m_FileCount++;
            }
        }
    }

    f_closedir(&dir);

    // Sort: directories first, then files alphabetically
    if (m_FileCount > 1)
        qsort(m_FileEntries, m_FileCount, sizeof(m_FileEntries[0]), compareFileEntriesDirectoriesFirst);

    LOGNOTE("SCSITBService::RefreshCacheInternal() Found %d entries (%d dirs + files)", m_FileCount, m_FileCount);
    return true;
}

// List specific folder by relative path (e.g., "Games/RPG" or "" for root)
bool SCSITBService::RefreshCacheForPath(const char* relativePath) {
    LOGNOTE("SCSITBService::RefreshCacheForPath() called with: %s",
            relativePath ? relativePath : "(null)");

    m_Lock.Acquire();

    char fullPath[MAX_PATH_LEN];
    if (relativePath == nullptr || relativePath[0] == '\0') {
        snprintf(fullPath, sizeof(fullPath), "1:/");
    } else {
        // Skip leading slash if present
        const char* pathPtr = relativePath;
        while (*pathPtr == '/')
            pathPtr++;

        snprintf(fullPath, sizeof(fullPath), "1:/%s", pathPtr);

        // Ensure trailing slash for directory
        size_t len = strlen(fullPath);
        if (len > 0 && len < sizeof(fullPath) - 1 && fullPath[len - 1] != '/') {
            fullPath[len] = '/';
            fullPath[len + 1] = '\0';
        }
    }

    LOGNOTE("SCSITBService::RefreshCacheForPath() full path: %s", fullPath);

    bool result = RefreshCacheInternal(fullPath);
    m_Lock.Release();

    LOGNOTE("SCSITBService::RefreshCacheForPath() completed, result: %d, count: %zu",
            result, m_FileCount);

    return result;
}

// Original RefreshCache for backwards compatibility and startup
bool SCSITBService::RefreshCache() {
    LOGNOTE("SCSITBService::RefreshCache() called");
    m_Lock.Acquire();

    // Get current loaded image from config
    const char* current_image = configservice->GetCurrentImage(DEFAULT_IMAGE_FILENAME);
    LOGNOTE("SCSITBService::RefreshCache() loaded current_image %s from config.txt", current_image);

    // Store the current image path if not already set
    if (m_CurrentImagePath[0] == '\0' && current_image != nullptr) {
        // If current_image contains a path (has /), use it directly
        // Otherwise assume it's in root
        if (strchr(current_image, '/') != nullptr) {
            snprintf(m_CurrentImagePath, sizeof(m_CurrentImagePath), "1:/%s", current_image);
        } else {
            snprintf(m_CurrentImagePath, sizeof(m_CurrentImagePath), "1:/%s", current_image);
        }
    }

    // Scan root directory for startup
    bool result = RefreshCacheInternal("1:/");
    if (!result) {
        m_Lock.Release();
        return false;
    }

    // Find the index of current_image in m_FileEntries (only check filename, not path)
    const char* currentFilename = current_image;
    // Extract just the filename if it's a path
    const char* lastSlash = strrchr(current_image, '/');
    if (lastSlash != nullptr)
        currentFilename = lastSlash + 1;

    bool found = false;
    for (size_t i = 0; i < m_FileCount; ++i) {
        if (!m_FileEntries[i].isDirectory && strcmp(m_FileEntries[i].name, currentFilename) == 0) {
            // If we don't yet have a current_cd e.g. we've just booted, then mount it
            if (current_cd < 0) {
                next_cd = i;
            } else {
                current_cd = i;
            }
            found = true;
            break;
        }
    }

    // If the current image is not found, fallback to the first available file (not directory)
    if (!found && m_FileCount > 0) {
        for (size_t i = 0; i < m_FileCount; ++i) {
            if (!m_FileEntries[i].isDirectory) {
                LOGNOTE("SCSITBService::RefreshCache() Current image '%s' not found. Fallback to '%s'", current_image, m_FileEntries[i].name);
                next_cd = i;
                break;
            }
        }
    }

    LOGNOTE("SCSITBService::RefreshCache() Found %d entries total", m_FileCount);
    m_Lock.Release();
    return true;
}

void SCSITBService::Run() {
    LOGNOTE("SCSITBService::Run started");

    while (true) {
        m_Lock.Acquire();

        // Handle load by full path (SetNextCDByPath was called)
        if (next_cd == -2) {
            LOGNOTE("Loading image by path: %s", m_CurrentImagePath);

            IImageDevice* imageDevice = loadImageDevice(m_CurrentImagePath);

            if (imageDevice == nullptr) {
                LOGERR("Failed to load image: %s", m_CurrentImagePath);
                next_cd = -1;
                m_Lock.Release();
                continue;
            }

            LOGNOTE("Loaded image: %s (format: %d, has subchannels: %s)",
                    m_CurrentImagePath,
                    (int)imageDevice->GetFileType(),
                    imageDevice->HasSubchannelData() ? "yes" : "no");

            cdromservice->SetDevice(imageDevice);

            // Save to config - store path without "1:/" prefix for compatibility
            const char* pathForConfig = m_CurrentImagePath;
            if (strncmp(pathForConfig, "1:/", 3) == 0)
                pathForConfig += 3;
            configservice->SetCurrentImage(pathForConfig);

            current_cd = -1;  // Index not meaningful for path-based load
            next_cd = -1;
        }
        // Handle load by index (SetNextCD or SetNextCDByName was called)
        else if (next_cd > -1) {
            if ((size_t)next_cd >= m_FileCount) {
                next_cd = -1;
                m_Lock.Release();
                continue;
            }

            // Build full path from current cache context
            // Note: For index-based loads, we assume the file is in root (backwards compatible)
            char* imageName = m_FileEntries[next_cd].name;
            snprintf(m_CurrentImagePath, sizeof(m_CurrentImagePath), "1:/%s", imageName);

            IImageDevice* imageDevice = loadImageDevice(m_CurrentImagePath);

            if (imageDevice == nullptr) {
                LOGERR("Failed to load image: %s", m_CurrentImagePath);
                next_cd = -1;
                m_Lock.Release();
                continue;
            }

            LOGNOTE("Loaded image: %s (format: %d, has subchannels: %s)",
                    m_CurrentImagePath,
                    (int)imageDevice->GetFileType(),
                    imageDevice->HasSubchannelData() ? "yes" : "no");

            cdromservice->SetDevice(imageDevice);

            // Save current mounted image name (just filename for backwards compatibility)
            configservice->SetCurrentImage(imageName);

            current_cd = next_cd;
            next_cd = -1;
        }

        m_Lock.Release();
        CScheduler::Get()->MsSleep(100);
    }
}
