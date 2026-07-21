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

// Match the real Circle logging macros so firmware sources that use
// LOGMODULE()/LOGNOTE()/LOGERR()/... (e.g. the disc-image readers) compile
// unchanged. Output is still gated by USBODE_TEST_VERBOSE in Write().
#define LOGMODULE(name)  static const char From[] = name
#define LOGPANIC(...)    CLogger::Get()->Write(From, LogPanic, __VA_ARGS__)
#define LOGERR(...)      CLogger::Get()->Write(From, LogError, __VA_ARGS__)
#define LOGWARN(...)     CLogger::Get()->Write(From, LogWarning, __VA_ARGS__)
#define LOGNOTE(...)     CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define LOGDBG(...)      CLogger::Get()->Write(From, LogDebug, __VA_ARGS__)

#endif
