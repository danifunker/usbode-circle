// Host-test stub: real firmware uses Circle's IRQ-based critical section.
// Single-threaded host tests don't need real exclusion.
#ifndef _test_stub_circle_synchronize_h
#define _test_stub_circle_synchronize_h

inline void EnterCritical(unsigned = 0) {}
inline void LeaveCritical(void) {}

#endif
