//
// Host-build stub for <circle/sched/task.h>.
//
#ifndef _circle_sched_task_h
#define _circle_sched_task_h

#include <circle/types.h>

class CTask
{
public:
    CTask(void) {}
    virtual ~CTask(void) {}

    virtual void Run(void) {}
};

#endif
