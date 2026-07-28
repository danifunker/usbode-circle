//
// Host-build stub for <circle/time.h>.
//
// Circle declares CTime here and pulls in time_t. Only time_t is used by the
// sources the suite compiles (it appears in the logger's event signatures), so
// this hands that straight to the host's own <time.h> instead of restating it.
//
#ifndef _circle_time_h
#define _circle_time_h

#include <time.h>

#endif
