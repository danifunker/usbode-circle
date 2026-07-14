// Host-test stub for Circle's CTimer. Provides a deterministic, manually
// advanceable clock instead of reading real hardware ticks.
#ifndef _test_stub_circle_timer_h
#define _test_stub_circle_timer_h

#include <circle/types.h>

#define CLOCKHZ 1000000

class CTimer
{
public:
    static u64 GetClockTicks() { return s_nTicks; }
    static void AdvanceForTest(u64 nDelta) { s_nTicks += nDelta; }

private:
    static u64 s_nTicks;
};

#endif
