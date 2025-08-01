#ifndef IMAGEDIRECTORYCACHE_H
#define IMAGEDIRECTORYCACHE_H

#include <fatfs/ff.h>
#include <stddef.h>
#include <stdint.h>
#include <circle/sched/task.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdromservice/cdromservice.h>
#include <configservice/configservice.h>

#define MAX_FILES 2048
#define MAX_FILENAME_LEN 255

struct FileEntry {
    char name[MAX_FILENAME_LEN];
    DWORD size; 
};

class SCSITBService : public CTask {
public:
    SCSITBService();
    ~SCSITBService();
    size_t GetCount() const;
    const char* GetName(size_t index) const;
    const char* GetCurrentCDName();
    DWORD GetSize(size_t index) const;
    const FileEntry* GetFileEntry(size_t index) const;
    FileEntry* begin();
    FileEntry* end();

    bool RefreshCache();

    void Run(void);
    bool SetNextCD(size_t index);
    bool SetNextCDByName(const char* file_name);
    size_t GetCurrentCD();

private:

    static SCSITBService *s_pThis;
    CDROMService* cdromservice = nullptr;
    ConfigService* configservice = nullptr;
    FileEntry* m_FileEntries;
    size_t m_FileCount;
    int next_cd = -1;
    int current_cd = -1;
};

#endif
