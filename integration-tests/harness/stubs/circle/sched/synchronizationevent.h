//
// Host-build stub for <circle/sched/synchronizationevent.h>.
//
#ifndef _circle_sched_synchronizationevent_h
#define _circle_sched_synchronizationevent_h

// The state is tracked rather than discarded so a test can tell whether a
// daemon was woken. Wait() cannot block: the host build is single-threaded and
// there is no scheduler to yield to, so a task under test is driven one step at
// a time instead of by running its loop.
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
