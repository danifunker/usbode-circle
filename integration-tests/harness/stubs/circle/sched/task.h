//
// Host-build stub for <circle/sched/task.h>.
//
#ifndef _circle_sched_task_h
#define _circle_sched_task_h

#include <circle/types.h>

#include <string.h>

class CTask
{
public:
    CTask(void) { m_Name[0] = '\0'; }
    virtual ~CTask(void) {}

    virtual void Run(void) {}

    // Circle finds tasks by name through CScheduler::GetTask(), so the stub has
    // to keep the name rather than discard it.
    void SetName(const char *pName)
    {
        if (pName == nullptr)
        {
            m_Name[0] = '\0';
            return;
        }
        strncpy(m_Name, pName, sizeof(m_Name) - 1);
        m_Name[sizeof(m_Name) - 1] = '\0';
    }

    const char *GetName(void) const { return m_Name; }

private:
    char m_Name[32];
};

#endif
