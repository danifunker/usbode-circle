#ifndef IMAGEDIRECTORYCACHE_H
#define IMAGEDIRECTORYCACHE_H

#include <fatfs/ff.h>
#include <stddef.h>
#include <stdint.h>
#include <circle/sched/task.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdromservice/cdromservice.h>
#include <configservice/configservice.h>
#include <circle/genericlock.h>

#define MAX_FILES 2048
#define MAX_FILENAME_LEN 255
#define MAX_PATH_LEN 512

struct FileEntry
{
    char name[MAX_FILENAME_LEN];          // Display name only (e.g., "game.iso")
    char relativePath[MAX_PATH_LEN];     // Full path from root (e.g., "Games/RPG/game.iso" or "Games/RPG" for folders)
    DWORD size;
    bool isDirectory;
};

class SCSITBService : public CTask
{
public:
    SCSITBService();
    ~SCSITBService();

    // Accessors
    size_t GetCount() const;
    const char* GetName(size_t index) const;
    const char* GetRelativePath(size_t index) const;
    DWORD GetSize(size_t index) const;
    const FileEntry* GetFileEntry(size_t index) const;
    FileEntry* begin();
    FileEntry* end();
    const char* GetCurrentCDName();
    size_t GetCurrentCD();
    bool IsDirectory(size_t index) const;

    // Path-aware accessors
    const char* GetCurrentCDPath() const;    // Full path of mounted image
    const char* GetCurrentCDFolder() const;  // Folder portion only (without "1:/")
    void GetFullPath(size_t index, char* outPath, size_t maxLen, const char* basePath) const;

    // Modifiers
    bool RefreshCache();  // Scan entire tree once
    bool SetNextCD(size_t index);
    bool SetNextCDByName(const char* file_name);

    // What a mount attempt did. Callers that report to the user want this and
    // MountByNameAndWait(); SetNextCDByName()'s "true" only means the name exists.
    enum class MountOutcome
    {
        Success,
        NotFound,  // no such entry; nothing was attempted
        Failed,    // tried and would not load, see GetLastMountError()
        Timeout    // still loading; not known to have failed
    };

    // Queue an image and wait for the service task to finish loading it.
    // TASK CONTEXT ONLY: this sleeps, so calling it from an interrupt handler
    // hard-locks the Pi. The sh1106 and st7789 button handlers run in the GPIO
    // interrupt, so they queue with SetNextCDByName() and poll GetMountSeq().
    // Not with m_Lock held either.
    MountOutcome MountByNameAndWait(const char* file_name, unsigned timeoutMs = 8000);

    // Bumped once per mount request the service task retires, whatever the result.
    unsigned GetMountSeq() const { return m_MountSeq; }

    // Eject / insert the current medium. These queue the request for the Run()
    // loop (task context), matching how SetNextCD defers the actual work.
    void SetPendingEject();
    void SetPendingInsert();
    bool IsEjected() const;  // delegates to cdromservice

    // Why the last mount attempt failed, or "" if it worked.
    const char* GetLastMountError() const { return m_LastMountError; }

    // The image config remembered, when it is no longer on the card, or "".
    // The drive comes up empty in that case; a substitute is still adopted
    // behind the scenes so the gadget has a geometry, see ProcessPendingMount().
    const char* GetMissingSavedImage() const { return m_MissingSavedImage; }

    // Task entry point
    void Run(void);

private:
    static SCSITBService* s_pThis;

    CDROMService* cdromservice = nullptr;
    ConfigService* configservice = nullptr;

    FileEntry* m_FileEntries = nullptr;
    size_t m_FileCount = 0;

    int next_cd = -1;
    int current_cd = -1;

    bool m_bPendingEject = false;
    bool m_bPendingInsert = false;

    // Eject-state persistence. m_bBootEjectPending latches the saved "was
    // ejected" state read once at startup, applied after the initial image
    // loads so the drive comes up empty. m_bPersistedEjected mirrors what is
    // currently written to config, so the Run loop only writes on a change.
    bool m_bBootEjectPending = false;
    bool m_bPersistedEjected = false;

    // Set by ProcessPendingMount() on every failure path, cleared on success.
    // 320 because at 160 the split-track message was cut off mid-word on screen.
    char m_LastMountError[320] = {0};

    // The image that is ACTUALLY mounted, written only after a load succeeds.
    // current_cd is an index into a list RefreshCache rebuilds, so it cannot
    // survive a rescan on its own; it is re-derived from this.
    char m_MountedRelativePath[MAX_PATH_LEN] = {0};

    volatile unsigned m_MountSeq = 0;

    // Kept until the user mounts something deliberately, so the explanation
    // survives a reboot.
    char m_MissingSavedImage[MAX_PATH_LEN] = {0};

    // Set by RefreshCache when the image it is about to mount is only a stand-in
    // for a missing one; ProcessPendingMount arms the boot-eject so it reads empty.
    bool m_bAdoptAsEmpty = false;

    // So the fallback in RefreshCache does not keep picking the same bad image.
    char m_LastFailedRelativePath[MAX_PATH_LEN] = {0};

    // Full path of currently mounted image (e.g., "1:/Games/game.iso")
    char m_CurrentImagePath[MAX_PATH_LEN];

    mutable CGenericLock m_Lock;

    void ClearCache();
    void ProcessPendingMount();  // called from Run() with m_Lock held
    void ScanDirectoryRecursive(const char* fullPath, const char* relativePath);  // Recursive scanner
};

#endif

