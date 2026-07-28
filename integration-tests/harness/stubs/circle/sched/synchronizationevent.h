//
// Host-build stub for <circle/sched/synchronizationevent.h>.
//
#ifndef _circle_sched_synchronizationevent_h
#define _circle_sched_synchronizationevent_h

// State is tracked so a test can tell whether a daemon was woken. Wait() cannot
// block on a single-threaded host, so tasks are driven one step at a time.
class CSynchronizationEvent
{
public:
    CSynchronizationEvent(void) : m_bState(false) {}
    void Set(void) { m_bState = true; }
    void Clear(void) { m_bState = false; }
    void Wait(void) {}
    bool GetState(void) const { return m_bState; }

private:
    bool m_bState;
};

#endif
