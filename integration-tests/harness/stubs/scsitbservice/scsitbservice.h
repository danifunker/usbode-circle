//
// Host-build stub for <scsitbservice/scsitbservice.h>.
// Fake file catalog for the vendor toolbox commands (0xD0/0xD2/0xD8/0xD9).
//
#ifndef _scsitbservice_scsitbservice_h
#define _scsitbservice_scsitbservice_h

#include <circle/sched/task.h>
#include <circle/types.h>

#include <string>
#include <vector>

typedef u32 DWORD;

class SCSITBService : public CTask
{
public:
    struct Entry
    {
        std::string name;
        DWORD size;
    };

    SCSITBService(void) {}

    size_t GetCount() const { return entries.size(); }

    const char *GetName(size_t index) const
    {
        return index < entries.size() ? entries[index].name.c_str() : "";
    }

    DWORD GetSize(size_t index) const
    {
        return index < entries.size() ? entries[index].size : 0;
    }

    bool SetNextCD(size_t index)
    {
        lastSetNextCD = (int)index;
        setNextCDCalls++;
        return true;
    }

    // Test-visible state
    std::vector<Entry> entries;
    int lastSetNextCD = -1;
    int setNextCDCalls = 0;
};

#endif
