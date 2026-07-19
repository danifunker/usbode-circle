//
// Host-build stub for <circle/sched/scheduler.h>.
// GetTask() resolves names from a test-populated registry, so tests decide
// which services (cdplayer, configservice, scsitbservice) "exist".
//
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h

#include <circle/sched/task.h>
#include <circle/timer.h> // Circle's scheduler.h pulls this in transitively
#include <circle/types.h>

class CScheduler
{
public:
    static CScheduler *Get(void);

    CTask *GetTask(const char *pTaskName);

    void Sleep(unsigned nSeconds) {}
    void MsSleep(unsigned nMilliSeconds) {}
    void usSleep(unsigned nMicroSeconds) {}
    void Yield(void) {}

    // Test control
    void TestRegisterTask(const char *pName, CTask *pTask);
    void TestClearTasks(void);
};

#endif
