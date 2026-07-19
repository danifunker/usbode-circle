//
// Host-build stub for <circle/logger.h>.
// Messages are printed to stdout only when USBODE_TEST_VERBOSE is set in
// the environment, so test output stays readable by default.
//
#ifndef _circle_logger_h
#define _circle_logger_h

#include <circle/types.h>

enum TLogSeverity
{
    LogPanic,
    LogError,
    LogWarning,
    LogNotice,
    LogDebug
};

class CLogger
{
public:
    static CLogger *Get(void);

    void Write(const char *pSource, TLogSeverity Severity, const char *pMessage, ...);
};

#endif
