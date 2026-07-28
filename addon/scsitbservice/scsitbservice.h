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

    /// What a mount attempt did. Anything that can tell the user should use
    /// MountByNameAndWait() and report this, rather than SetNextCDByName(),
    /// whose "true" only means the name was found in the catalogue.
    enum class MountOutcome
    {
        Success,
        NotFound,  // no such entry; nothing was attempted
        Failed,    // the image was tried and would not load, see GetLastMountError()
        Timeout    // still loading; too big or the card is slow, not known to have failed
    };

    /// Queue an image and wait for the service task to finish loading it.
    ///
    /// Mounting is asynchronous - the request is handed to Run() and picked up
    /// on its next pass - so a caller that returns as soon as it has queued the
    /// request can only report that the file exists. The web UI announced
    /// "Successfully mounted" for images that then failed to load, which is how
    /// a split-track cue came to report success and refusal on two consecutive
    /// pages.
    ///
    /// TASK CONTEXT ONLY. This sleeps on the scheduler, so calling it from an
    /// interrupt handler freezes the machine hard enough to need a power cycle.
    /// That is not hypothetical: the sh1106 and st7789 button handlers run in
    /// the GPIO interrupt (see PageManager::HandleButtonPress), and calling
    /// this from there locked up the Pi on every mount. Those pages queue with
    /// SetNextCDByName() and watch GetMountSeq() from their refresh loop
    /// instead. Also not with m_Lock held, and not from a SCSI command handler
    /// - the vendor toolbox picker has no way to show a result anyway.
    MountOutcome MountByNameAndWait(const char* file_name, unsigned timeoutMs = 8000);

    /// Counter of mount requests the service task has finished with, whatever
    /// the result. Changes exactly once per retired request.
    unsigned GetMountSeq() const { return m_MountSeq; }

    // Eject / insert the current medium. These queue the request for the Run()
    // loop (task context), matching how SetNextCD defers the actual work.
    void SetPendingEject();
    void SetPendingInsert();
    bool IsEjected() const;  // delegates to cdromservice

    /// Why the last mount attempt failed, or "" if the last one worked.
    ///
    /// Mounting is asynchronous: SetNextCDByName() only queues an index, so
    /// the web UI's "ok" says the name was found in the catalog, not that the
    /// image loaded. When the load then fails the old disc stays mounted and
    /// the only record is a line in the log, which is how an image that cannot
    /// be mounted looks to the user exactly like one that can.
    const char* GetLastMountError() const { return m_LastMountError; }

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
    // Wide enough for the loader's own wording plus the file name; at 160 the
    // split-track message was cut off mid-word on screen.
    char m_LastMountError[320] = {0};

    // Relative path of the image that is ACTUALLY mounted, written only after
    // a load succeeds. current_cd is an index into a list that RefreshCache
    // rebuilds, so it cannot survive a rescan on its own - hiding .bin files
    // shifted every index and left "Current File Loaded" naming a file that
    // had never been mounted. This is what current_cd is re-derived from.
    char m_MountedRelativePath[MAX_PATH_LEN] = {0};

    // Bumped once per mount request the service task retires, so a caller can
    // tell "still queued" from "dealt with" without polling internal state.
    volatile unsigned m_MountSeq = 0;

    // Relative path of the last image that failed to load, so the
    // pick-something fallback in RefreshCache does not keep choosing it and
    // failing again on every rescan.
    char m_LastFailedRelativePath[MAX_PATH_LEN] = {0};

    // Full path of currently mounted image (e.g., "1:/Games/game.iso")
    char m_CurrentImagePath[MAX_PATH_LEN];

    mutable CGenericLock m_Lock;

    void ClearCache();
    void ProcessPendingMount();  // called from Run() with m_Lock held
    void ScanDirectoryRecursive(const char* fullPath, const char* relativePath);  // Recursive scanner
};

#endif

