//
// Host-build stub for <circle/timer.h>.
// Tick time is virtual and test-controlled: tests advance it explicitly
// with TestAdvanceTicks() to exercise time-dependent paths (disc-swap
// settle window) deterministically.
//
#ifndef _circle_timer_h
#define _circle_timer_h

#include <circle/types.h>

#define CLOCKHZ 1000000
#ifndef HZ
#define HZ 100
#endif

class CTimer
{
public:
    static CTimer *Get(void);

    unsigned GetTicks(void);
    unsigned GetClockTicks(void);
    void MsDelay(unsigned nMilliSeconds) {}
    void usDelay(unsigned nMicroSeconds) {}

    // Test control
    void TestAdvanceTicks(unsigned nTicks);
    void TestReset(void);
};

#endif
