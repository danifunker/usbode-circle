//
// Host-build stub for <circle/logger.h>.
// Messages are printed to stdout only when USBODE_TEST_VERBOSE is set in
// the environment, so test output stays readable by default.
//
#ifndef _circle_logger_h
#define _circle_logger_h

#include <circle/types.h>
#include <time.h>

enum TLogSeverity
{
    LogPanic,
    LogError,
    LogWarning,
    LogNotice,
    LogDebug
};

// Sizes as declared by the real circle/logger.h, since CFileLogDaemon sizes
// its own ReadEvent() buffers from them.
#define LOG_MAX_SOURCE   50
#define LOG_MAX_MESSAGE  200

typedef void TLogEventNotificationHandler(void);
typedef void TLogPanicHandler(void);

class CLogger
{
public:
    static CLogger *Get(void);

    void Write(const char *pSource, TLogSeverity Severity, const char *pMessage, ...);

    // The real CLogger queues EVERY event whatever the loglevel; consumers
    // filter as they drain. Dropping here would hide a daemon filtering wrongly.
    boolean ReadEvent(TLogSeverity *pSeverity, char *pSource, char *pMessage,
                      time_t *pTime, unsigned *pHundredthTime, int *pTimeZone);

    void RegisterEventNotificationHandler(TLogEventNotificationHandler *pHandler);
    void RegisterPanicHandler(TLogPanicHandler *pHandler);

    // Test-side control: queue an event as firmware would, and empty the queue
    // between tests so one test's chatter is not read by the next.
    static void TestQueueEvent(TLogSeverity Severity, const char *pSource, const char *pMessage);
    static void TestClearEvents(void);
    static unsigned TestQueuedEventCount(void);
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
