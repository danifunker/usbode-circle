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

struct FileEntry
{
    char name[MAX_FILENAME_LEN];
    DWORD size;
};

class SCSITBService : public CTask
{
public:
    SCSITBService();
    ~SCSITBService();

    // Accessors
    size_t GetCount() const;
    const char* GetName(size_t index) const;
    DWORD GetSize(size_t index) const;
    const FileEntry* GetFileEntry(size_t index) const;
    FileEntry* begin();
    FileEntry* end();
    const char* GetCurrentCDName();
    size_t GetCurrentCD();

    // Modifiers
    bool RefreshCache();
    bool SetNextCD(size_t index);
    bool SetNextCDByName(const char* file_name);

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

    mutable CGenericLock m_Lock;

    void ClearCache();
};

#endif

