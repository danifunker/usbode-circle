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

// Sort directories first, then files alphabetically by full path
int compareFileEntriesDirectoriesFirst(const void* a, const void* b) {
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;

    // Directories come first
    if (fa->isDirectory && !fb->isDirectory) return -1;
    if (!fa->isDirectory && fb->isDirectory) return 1;

    // Within same type, sort alphabetically by full path (case-insensitive)
    return strcasecmp(fa->relativePath, fb->relativePath);
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

const char* SCSITBService::GetRelativePath(size_t index) const {
    if (index >= m_FileCount)
        return nullptr;
    return m_FileEntries[index].relativePath;
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
		//LOGNOTE("SCSITBService::SetNextCDByName testing %s", it->relativePath);
                // Compare against relativePath to support files in subfolders
                if (strcmp(file_name, it->relativePath) == 0) {
                        return SetNextCD(index);
                }
        }

	return false;
}

// SetNextCDByPath removed - use SetNextCDByName with relativePath instead


// Recursive scanner that stores full relative paths
void SCSITBService::ScanDirectoryRecursive(const char* fullPath, const char* relativePath) {
    LOGNOTE("SCSITBService::ScanDirectoryRecursive() scanning: %s (relative: %s)", fullPath, relativePath);

    DIR dir;
    FRESULT fr = f_opendir(&dir, fullPath);
    if (fr != FR_OK) {
        LOGERR("SCSITBService::ScanDirectoryRecursive() failed to open: %s (error: %d)", fullPath, fr);
        return;
    }

    FILINFO fno;
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;

        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0)
            continue;

        // Exclude hidden files/folders
        if (fno.fname[0] == '.')
            continue;

        // Exclude Mac cache files
        if (strncmp(fno.fname, "._", 2) == 0)
            continue;

        // Exclude system folders
        if (strcasecmp(fno.fname, "System Volume Information") == 0 ||
            strcasecmp(fno.fname, "$RECYCLE.BIN") == 0 ||
            strcasecmp(fno.fname, "RECYCLER") == 0 ||
            strcasecmp(fno.fname, "lost+found") == 0)
            continue;

        if (m_FileCount >= MAX_FILES) {
            LOGERR("SCSITBService: MAX_FILES limit reached!");
            break;
        }

        // Build relative path for this entry
        char entryRelativePath[MAX_PATH_LEN];
        if (relativePath[0] == '\0') {
            snprintf(entryRelativePath, sizeof(entryRelativePath), "%s", fno.fname);
        } else {
            snprintf(entryRelativePath, sizeof(entryRelativePath), "%s/%s", relativePath, fno.fname);
        }

        if (fno.fattrib & AM_DIR) {
            // Store folder entry
            size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
            memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
            m_FileEntries[m_FileCount].name[len] = '\0';
            
            size_t pathLen = my_strnlen(entryRelativePath, MAX_PATH_LEN - 1);
            memcpy(m_FileEntries[m_FileCount].relativePath, entryRelativePath, pathLen);
            m_FileEntries[m_FileCount].relativePath[pathLen] = '\0';
            
            m_FileEntries[m_FileCount].size = 0;
            m_FileEntries[m_FileCount].isDirectory = true;
            m_FileCount++;

            // Recurse into subdirectory
            char subFullPath[MAX_PATH_LEN];
            snprintf(subFullPath, sizeof(subFullPath), "%s/%s", fullPath, fno.fname);
            ScanDirectoryRecursive(subFullPath, entryRelativePath);
        } else {
            // Check for image files
            const char* ext = strrchr(fno.fname, '.');
            if (ext != nullptr) {
                if (iequals(ext, ".iso") || iequals(ext, ".bin") || iequals(ext, ".cue") ||
                    iequals(ext, ".mds") || iequals(ext, ".chd") || iequals(ext, ".ccd") ||
                    iequals(ext, ".toast")) {
                    size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
                    memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
                    m_FileEntries[m_FileCount].name[len] = '\0';
                    
                    size_t pathLen = my_strnlen(entryRelativePath, MAX_PATH_LEN - 1);
                    memcpy(m_FileEntries[m_FileCount].relativePath, entryRelativePath, pathLen);
                    m_FileEntries[m_FileCount].relativePath[pathLen] = '\0';
                    
                    m_FileEntries[m_FileCount].size = fno.fsize;
                    m_FileEntries[m_FileCount].isDirectory = false;
                    m_FileCount++;
                }
            }
        }
    }

    f_closedir(&dir);
}

// Remove entries for data files that are referenced by a cue sheet in the
// same directory; the cue itself stays listed and is the mountable entry.
void SCSITBService::HideCueCoveredBins() {
    if (m_FileCount == 0)
        return;

    bool* remove = new bool[m_FileCount]();

    for (size_t i = 0; i < m_FileCount; ++i) {
        if (m_FileEntries[i].isDirectory)
            continue;
        const char* ext = strrchr(m_FileEntries[i].name, '.');
        if (ext == nullptr || !iequals(ext, ".cue"))
            continue;

        // Directory part of the cue's relative path (with trailing '/')
        const char* rel = m_FileEntries[i].relativePath;
        const char* lastSlash = strrchr(rel, '/');
        size_t dirLen = lastSlash ? (size_t)(lastSlash - rel + 1) : 0;

        char fullPath[MAX_PATH_LEN + 4];
        snprintf(fullPath, sizeof(fullPath), "1:/%s", rel);

        char* cueText = nullptr;
        if (!ReadFileToString(fullPath, &cueText))
            continue;

        // Hide every data file referenced by a FILE "name" line
        const char* p = cueText;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r')
                p++;
            const char* lineEnd = strchr(p, '\n');

            if (strncasecmp(p, "FILE", 4) == 0) {
                const char* q = strchr(p, '"');
                if (q != nullptr && (lineEnd == nullptr || q < lineEnd)) {
                    q++;
                    const char* qe = strchr(q, '"');
                    if (qe != nullptr && (size_t)(qe - q) < MAX_FILENAME_LEN) {
                        char refPath[MAX_PATH_LEN];
                        snprintf(refPath, sizeof(refPath), "%.*s%.*s",
                                 (int)dirLen, rel, (int)(qe - q), q);

                        for (size_t j = 0; j < m_FileCount; ++j) {
                            if (j != i && !m_FileEntries[j].isDirectory && !remove[j] &&
                                strcasecmp(m_FileEntries[j].relativePath, refPath) == 0) {
                                LOGNOTE("Hiding %s (referenced by %s)",
                                        m_FileEntries[j].relativePath, rel);
                                remove[j] = true;
                            }
                        }
                    }
                }
            }

            if (lineEnd == nullptr)
                break;
            p = lineEnd + 1;
        }

        delete[] cueText;
    }

    // Compact the array
    size_t w = 0;
    for (size_t j = 0; j < m_FileCount; ++j) {
        if (!remove[j]) {
            if (w != j)
                m_FileEntries[w] = m_FileEntries[j];
            w++;
        }
    }
    m_FileCount = w;
    delete[] remove;
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

    // Scan entire tree recursively
    m_FileCount = 0;
    ScanDirectoryRecursive("1:/", "");

    // Hide bin files that belong to a cue sheet: the cue is the mountable
    // entry (multi-bin images would otherwise list dozens of track bins
    // that cannot be mounted individually)
    HideCueCoveredBins();

    // Sort all entries: directories first, then alphabetically
    if (m_FileCount > 1) {
        qsort(m_FileEntries, m_FileCount, sizeof(FileEntry), compareFileEntriesDirectoriesFirst);
    }
    
    LOGNOTE("SCSITBService::RefreshCache() Found %d total entries", (int)m_FileCount);

    // Find the current image in cache by matching relative path
    const char* searchPath = current_image;
    bool found = false;
    
    if (searchPath && searchPath[0] != '\0') {
        for (size_t i = 0; i < m_FileCount; ++i) {
            if (!m_FileEntries[i].isDirectory && strcmp(m_FileEntries[i].relativePath, searchPath) == 0) {
                if (current_cd < 0) {
                    next_cd = i;
                }
                found = true;
                LOGNOTE("SCSITBService::RefreshCache() Found current image at index %d", (int)i);
                break;
            }
        }
    }

    // Fallback to first image file if not found
    if (!found && m_FileCount > 0) {
        for (size_t i = 0; i < m_FileCount; ++i) {
            if (!m_FileEntries[i].isDirectory) {
                LOGNOTE("SCSITBService::RefreshCache() Current image not found, using: %s", 
                        m_FileEntries[i].relativePath);
                next_cd = i;
                break;
            }
        }
    }

    m_Lock.Release();
    return true;
}

void SCSITBService::Run() {
    LOGNOTE("SCSITBService::Run started");

    while (true) {
        m_Lock.Acquire();

        // Handle load by index (SetNextCD or SetNextCDByName was called)
        if (next_cd > -1) {
            if ((size_t)next_cd >= m_FileCount) {
                next_cd = -1;
                m_Lock.Release();
                continue;
            }

            // Build full path using relativePath from cache
            const char* relativePath = m_FileEntries[next_cd].relativePath;
            // Ensure we have room for "1:/" prefix (3 chars) + relativePath + null terminator
            if (strlen(relativePath) > MAX_PATH_LEN - 4) {
                LOGERR("Path too long: %s", relativePath);
                next_cd = -1;
                m_Lock.Release();
                continue;
            }
            snprintf(m_CurrentImagePath, sizeof(m_CurrentImagePath), "1:/%s", relativePath);

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

            // Save relative path to config (without "1:/" prefix)
            configservice->SetCurrentImage(relativePath);

            current_cd = next_cd;
            next_cd = -1;
        }

        m_Lock.Release();
        CScheduler::Get()->MsSleep(100);
    }
}
