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

    // Read the persisted eject state once at startup, before anything can mount.
    // If the drive was ejected when last powered off, come up as an empty drive:
    // the remembered image still loads (so the gadget knows the geometry and
    // Insert is instant) but the gadget never reports it ready. Arming this
    // before any SetDevice() is what makes it reliable - ejecting after the
    // mount left a window in which the host could enumerate, see a disc and
    // mount it.
    if (configservice) {
        m_bBootEjectPending = configservice->GetEjected();
        m_bPersistedEjected = m_bBootEjectPending;
        if (m_bBootEjectPending) {
            LOGNOTE("Boot: drive was ejected at power-off, coming up empty");
            cdromservice->ArmBootEject();
        }
    }

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

void SCSITBService::SetPendingEject() {
    m_Lock.Acquire();
    m_bPendingEject = true;
    m_bPendingInsert = false;
    m_Lock.Release();
}

void SCSITBService::SetPendingInsert() {
    m_Lock.Acquire();
    m_bPendingInsert = true;
    m_bPendingEject = false;
    m_Lock.Release();
}

bool SCSITBService::IsEjected() const {
    return cdromservice && cdromservice->IsEjected();
}

const char* SCSITBService::GetCurrentCDName() {
	// Never nullptr: current_cd is -1 when nothing is mounted, which GetName()
	// answers with nullptr, and callers feed this straight into std::string.
	const char* name = GetName(GetCurrentCD());
	return name != nullptr ? name : "";
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
                bool listIt = iequals(ext, ".iso") || iequals(ext, ".mds") ||
                              iequals(ext, ".chd") || iequals(ext, ".toast");
                if (!listIt && iequals(ext, ".cue")) {
                    // Even with no same-stem .bin: hiding those also hid
                    // split-track rips, whose data is "Game (Track 1).bin".
                    listIt = true;
                }
                // A .bin is never listed: mounting one reads the .cue first, so
                // it is either unmountable or already represented by that cue.
                if (listIt) {
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

// Original RefreshCache for backwards compatibility and startup
bool SCSITBService::RefreshCache() {
    LOGNOTE("SCSITBService::RefreshCache() called");
    m_Lock.Acquire();

    // Empty default, not DEFAULT_IMAGE_FILENAME, which conflates a remembered
    // image that has gone missing with a fresh card that never had one.
    const char* saved_image = configservice->GetCurrentImage("");
    const bool hadRememberedImage = (saved_image != nullptr && saved_image[0] != '\0');
    const char* current_image = hadRememberedImage ? saved_image : DEFAULT_IMAGE_FILENAME;
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
    
    // Sort all entries: directories first, then alphabetically
    if (m_FileCount > 1) {
        qsort(m_FileEntries, m_FileCount, sizeof(FileEntry), compareFileEntriesDirectoriesFirst);
    }
    
    LOGNOTE("SCSITBService::RefreshCache() Found %d total entries", (int)m_FileCount);

    // The list was just rebuilt, so the old current_cd index may now name a
    // different file. Re-derive it from the path that actually got mounted.
    {
        int resolved = -1;
        if (m_MountedRelativePath[0] != '\0') {
            for (size_t i = 0; i < m_FileCount; ++i) {
                if (!m_FileEntries[i].isDirectory &&
                    strcmp(m_FileEntries[i].relativePath, m_MountedRelativePath) == 0) {
                    resolved = (int)i;
                    break;
                }
            }
        }
        if (resolved != current_cd) {
            LOGNOTE("SCSITBService::RefreshCache() current index %d -> %d after rescan",
                    current_cd, resolved);
        }
        current_cd = resolved;
    }

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

    // A saved .bin whose cue/bin pair is now listed as the .cue: retry
    // with the .cue path, so upgrading doesn't silently switch the
    // mounted disc to the first entry
    if (!found && searchPath && searchPath[0] != '\0') {
        size_t len = strlen(searchPath);
        if (len >= 4 && len < MAX_PATH_LEN && iequals(searchPath + len - 4, ".bin")) {
            char cuePath[MAX_PATH_LEN];
            memcpy(cuePath, searchPath, len - 4);
            strcpy(cuePath + len - 4, ".cue");
            for (size_t i = 0; i < m_FileCount; ++i) {
                if (!m_FileEntries[i].isDirectory && strcasecmp(m_FileEntries[i].relativePath, cuePath) == 0) {
                    if (current_cd < 0) {
                        next_cd = i;
                    }
                    found = true;
                    LOGNOTE("SCSITBService::RefreshCache() Current image %s now listed as %s (index %d)",
                            searchPath, m_FileEntries[i].relativePath, (int)i);
                    break;
                }
            }
        }
    }

    // Fallback to the first image, but not while ejected: this also runs on
    // rescans, where mounting a substitute would undo the user's eject. Unless
    // the gadget has never come up, since adopting is what initializes it.
    if (!found && m_FileCount > 0 &&
        (!IsEjected() || (cdromservice && !cdromservice->IsGadgetInitialized()))) {
        for (size_t i = 0; i < m_FileCount; ++i) {
            if (m_FileEntries[i].isDirectory) {
                continue;
            }
            // Skip the one we already know will not load, or every rescan picks
            // it again and re-raises the error banner.
            if (m_LastFailedRelativePath[0] != '\0' &&
                strcmp(m_FileEntries[i].relativePath, m_LastFailedRelativePath) == 0) {
                continue;
            }
            LOGNOTE("SCSITBService::RefreshCache() Current image not found, using: %s",
                    m_FileEntries[i].relativePath);
            next_cd = i;

            // A remembered image that is gone must not be silently swapped for a
            // different disc. Adopt this one so the gadget has a geometry, but
            // present the drive as empty and record what went missing.
            if (hadRememberedImage) {
                strncpy(m_MissingSavedImage, current_image, sizeof(m_MissingSavedImage) - 1);
                m_MissingSavedImage[sizeof(m_MissingSavedImage) - 1] = '\0';
                m_bAdoptAsEmpty = true;
                LOGWARN("Saved image %s is not on the card; coming up empty with %s adopted",
                        current_image, m_FileEntries[i].relativePath);
            }

            // SetDevice() clears the ejected latch unless the boot-eject is armed,
            // so without this the adoption above would insert a disc the user ejected.
            if (IsEjected()) {
                m_bAdoptAsEmpty = true;
            }
            break;
        }
    }

    m_Lock.Release();
    return true;
}

// Mount whatever SetNextCD/SetNextCDByName queued. Split out of Run() so every
// failure path can simply return: the caller still has to consume the one-shot
// boot-eject arm, which an early `continue` in the loop used to skip.
// Called with m_Lock held.
void SCSITBService::ProcessPendingMount() {
    if (next_cd <= -1)
        return;

    // Consume up front: every exit path below retires the request, so leaving it
    // set would eject the next image the user deliberately mounts.
    const bool adoptAsEmpty = m_bAdoptAsEmpty;
    m_bAdoptAsEmpty = false;

    if ((size_t)next_cd >= m_FileCount) {
        snprintf(m_LastMountError, sizeof(m_LastMountError),
                 "That image is no longer in the list; the card may have been rescanned.");
        next_cd = -1;
        m_MountSeq++;
        return;
    }

    m_LastMountError[0] = '\0';

    // Build full path using relativePath from cache
    const char* relativePath = m_FileEntries[next_cd].relativePath;
    // Ensure we have room for "1:/" prefix (3 chars) + relativePath + null terminator
    if (strlen(relativePath) > MAX_PATH_LEN - 4) {
        LOGERR("Path too long: %s", relativePath);
        snprintf(m_LastMountError, sizeof(m_LastMountError),
                 "Path is too long to mount: %s", relativePath);
        next_cd = -1;
        m_MountSeq++;
        return;
    }
    // Local, not m_CurrentImagePath: writing that before the load makes a failed
    // mount report a disc the host was never given.
    char candidatePath[MAX_PATH_LEN];
    snprintf(candidatePath, sizeof(candidatePath), "1:/%s", relativePath);

    IImageDevice* imageDevice = loadImageDevice(candidatePath);

    if (imageDevice == nullptr) {
        LOGERR("Failed to load image: %s", candidatePath);
        strncpy(m_LastFailedRelativePath, relativePath, sizeof(m_LastFailedRelativePath) - 1);
        m_LastFailedRelativePath[sizeof(m_LastFailedRelativePath) - 1] = '\0';
        // Prefer the loader's own reason; the generic wording below is only for
        // failure paths that have not been given words yet.
        const char* why = GetLastImageLoadError();
        if (why != nullptr && why[0] != '\0') {
            snprintf(m_LastMountError, sizeof(m_LastMountError),
                     "%s (%s) The previous disc is still mounted.", why, relativePath);
        } else {
            snprintf(m_LastMountError, sizeof(m_LastMountError),
                     "Could not load %s. It may be an unsupported or damaged image; "
                     "the log has the details. The previous disc is still mounted.",
                     relativePath);
        }
        next_cd = -1;
        m_MountSeq++;
        return;
    }

    LOGNOTE("Loaded image: %s (format: %d, has subchannels: %s)",
            candidatePath,
            (int)imageDevice->GetFileType(),
            imageDevice->HasSubchannelData() ? "yes" : "no");

    // Take the stand-in's geometry but keep the drive empty to the host. Armed
    // before SetDevice(), which decides the ejected state itself and can yield:
    // ejecting after it leaves a window where the host mounts the stand-in.
    if (adoptAsEmpty) {
        cdromservice->ArmBootEject();
    }

    cdromservice->SetDevice(imageDevice);

    // m_CurrentImagePath is the file the gadget holds open, set either way: the
    // delete/rename guards key off it, and a stand-in is as destructive to yank.
    strncpy(m_CurrentImagePath, candidatePath, sizeof(m_CurrentImagePath) - 1);
    m_CurrentImagePath[sizeof(m_CurrentImagePath) - 1] = '\0';

    // m_MountedRelativePath is what the USER mounted, which a stand-in is not.
    // Left clear it resolves current_cd to -1, so the UI highlights nothing.
    if (!adoptAsEmpty) {
        strncpy(m_MountedRelativePath, relativePath, sizeof(m_MountedRelativePath) - 1);
        m_MountedRelativePath[sizeof(m_MountedRelativePath) - 1] = '\0';
    } else {
        m_MountedRelativePath[0] = '\0';
    }
    m_LastFailedRelativePath[0] = '\0';

    // Save relative path to config (without "1:/" prefix).
    //
    // Not for a stand-in: recording it would make the substitute the user's
    // remembered image, so the explanation would vanish on the next boot and
    // putting the missing file back would no longer bring it up. Leaving the
    // old name in config means the notice persists until something is mounted
    // deliberately, and the fallback does not fire again because the drive is
    // now ejected.
    if (!adoptAsEmpty) {
        configservice->SetCurrentImage(relativePath);
        m_MissingSavedImage[0] = '\0';
    }

    // Nothing is current when the drive is empty, so the file list agrees with
    // what the host can see rather than contradicting it.
    current_cd = adoptAsEmpty ? -1 : next_cd;
    next_cd = -1;
    m_MountSeq++;
}

SCSITBService::MountOutcome SCSITBService::MountByNameAndWait(const char* file_name,
                                                             unsigned timeoutMs) {
    const unsigned startSeq = m_MountSeq;

    if (!SetNextCDByName(file_name)) {
        return MountOutcome::NotFound;
    }

    // Run() picks the request up on its next pass and retires it, bumping the
    // sequence exactly once whatever the result. Poll rather than block: this
    // runs on the web server's or the display's task, and the service task
    // needs the CPU to do the work being waited for.
    unsigned waited = 0;
    const unsigned step = 20;
    while (m_MountSeq == startSeq && waited < timeoutMs) {
        CScheduler::Get()->MsSleep(step);
        waited += step;
    }

    if (m_MountSeq == startSeq) {
        // A large CHD on a slow card can genuinely take a while. Saying it
        // failed would be a guess, and the wrong one more often than not.
        return MountOutcome::Timeout;
    }

    return m_LastMountError[0] == '\0' ? MountOutcome::Success : MountOutcome::Failed;
}

void SCSITBService::Run() {
    LOGNOTE("SCSITBService::Run started");

    while (true) {
        m_Lock.Acquire();

        // Handle load by index (SetNextCD or SetNextCDByName was called)
        ProcessPendingMount();

        // The boot-eject arm is one-shot and is consumed by the first
        // SetDevice(). If the remembered image never loaded (missing, renamed,
        // unreadable) no SetDevice() came, so clear it here - otherwise it would
        // sit armed and silently eject the next image the user mounts by hand.
        // The drive itself stays ejected either way, which is the saved state.
        if (m_bBootEjectPending) {
            m_bBootEjectPending = false;
            cdromservice->DisarmBootEject();
        }

        // Handle eject / insert requests (button, web, or deferred here from a
        // caller). Mounting a new image above implicitly re-inserts, so process
        // these after the load so a queued eject can't strand a fresh mount.
        if (m_bPendingEject) {
            m_bPendingEject = false;
            LOGNOTE("Ejecting current medium");
            cdromservice->Eject();
        }
        if (m_bPendingInsert) {
            m_bPendingInsert = false;
            LOGNOTE("Inserting current medium");
            cdromservice->Insert();
        }

        // Persist the eject state whenever it changes. The gadget is the source
        // of truth, so this captures every source uniformly - button, web, and
        // the host START STOP UNIT (which runs in IRQ context and must not touch
        // the SD card itself).
        if (configservice) {
            bool ejected = cdromservice->IsEjected();
            if (ejected != m_bPersistedEjected) {
                m_bPersistedEjected = ejected;
                configservice->SetEjected(ejected);
                LOGNOTE("Persisted eject state: %s", ejected ? "ejected" : "inserted");
            }
        }

        m_Lock.Release();
        CScheduler::Get()->MsSleep(100);
    }
}
