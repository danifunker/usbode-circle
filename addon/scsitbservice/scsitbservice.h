#ifndef IMAGEDIRECTORYCACHE_H
#define IMAGEDIRECTORYCACHE_H

#include <fatfs/ff.h>
#include <stddef.h>
#include <stdint.h>
#include <circle/sched/task.h>
#include <usbcdgadget/usbcdgadget.h>
#include <Properties/propertiesfatfsfile.h>

#define MAX_FILES 2048
#define MAX_FILENAME_LEN 255

struct FileEntry {
    char name[MAX_FILENAME_LEN];
    DWORD size; 
};

class SCSITBService : public CTask {
public:
    SCSITBService(CPropertiesFatFsFile *pProperties, CUSBCDGadget *pCDGadget);
    ~SCSITBService();
    size_t GetCount() const;
    const char* GetName(size_t index) const;
    DWORD GetSize(size_t index) const;

    bool RefreshCache();

    void Run(void);
    bool SetNextCD(int index);

private:

    static SCSITBService *s_pThis;
    CPropertiesFatFsFile *m_pProperties;
    CUSBCDGadget *m_pCDGadget;
    FileEntry *m_FileEntries;
    size_t m_FileCount;
    int next_cd = -1;
};

#endif
