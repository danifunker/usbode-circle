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

    // Sleeping cannot happen on a single-threaded host, but it is counted: on a
    // real Pi a sleep on a per-event path costs the whole system.
    void Sleep(unsigned nSeconds) { TestNoteSleep(); }
    void MsSleep(unsigned nMilliSeconds) { TestNoteSleep(); }
    void usSleep(unsigned nMicroSeconds) { TestNoteSleep(); }
    void Yield(void) {}

    // Test control
    void TestRegisterTask(const char *pName, CTask *pTask);
    void TestClearTasks(void);
    static void TestNoteSleep(void);
    static unsigned TestSleepCount(void);
    static void TestResetSleepCount(void);
};

#endif
