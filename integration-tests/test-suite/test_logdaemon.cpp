//
// test_logdaemon.cpp
//
// The file log daemon on the host FatFs seam, mostly about what it does when the
// file it was told to open is not there. Run() never returns, so the tests drive
// DrainOnce(), one pass of its loop.
//
#include "framework.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <fatfs/ff.h>
#include <filelogdaemon/filelogdaemon.h>

#include <stdio.h>
#include <string.h>

#include <new>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TestDataDir()
{
#ifdef USBODE_TESTDATA
    return USBODE_TESTDATA;
#else
    return "out/images";
#endif
}

static void QueueEvents(unsigned n, TLogSeverity severity = LogError)
{
    for (unsigned i = 0; i < n; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "event %u", i);
        CLogger::TestQueueEvent(severity, "test", msg);
    }
}

static std::string ReadWholeFile(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return std::string();
    }
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

static void RemoveFile(const std::string &path)
{
    remove(path.c_str());
}

// The rest of the suite logs freely into this same queue, so start each test clean.
static void ResetLogging()
{
    CLogger::TestClearEvents();
    CScheduler::TestResetSleepCount();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The headline defect: with no file open, the drain loop slept 20 ms per failed
// message, retiring 50 events a second and taking the scheduler down with it.
// The events must still be consumed, just not paid for.
TEST(logdaemon_unopenable_path_drains_without_sleeping)
{
    ResetLogging();

    // A missing directory, the shape of the reported value "0:/SD:/usbode-logs.txt".
    const std::string path = TestDataDir() + "/no-such-dir/usbode-logs.txt";

    CFileLogDaemon daemon(path.c_str(), 5);
    CHECK(!daemon.IsFileLogging());
    CHECK(daemon.GetOpenResult() != FR_OK);

    // The constructor logs its own failure; clear so the count below is ours.
    ResetLogging();

    const unsigned kEvents = 200;
    QueueEvents(kEvents);
    CHECK_EQ(CLogger::TestQueuedEventCount(), kEvents);

    daemon.DrainOnce();

    // Consumed, and for free.
    CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
    CHECK_EQ(CScheduler::TestSleepCount(), 0u);
}

// m_bFileInitialized had no initializer, so a failed open left it holding
// whatever was in that memory. Constructing over poisoned storage exposes it.
// Reading an invalid bool is undefined, so this asserts the behaviour rather
// than the flag; -fsanitize=undefined catches the read itself.
TEST(logdaemon_failed_open_leaves_the_file_flag_false)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/no-such-dir/usbode-logs.txt";

    alignas(CFileLogDaemon) static unsigned char storage[sizeof(CFileLogDaemon)];
    memset(storage, 0xAA, sizeof(storage));

    CFileLogDaemon *daemon = new (storage) CFileLogDaemon(path.c_str(), 5);
    CHECK(!daemon->IsFileLogging());

    ResetLogging();
    QueueEvents(4);
    daemon->DrainOnce();
    CHECK_EQ(CScheduler::TestSleepCount(), 0u);

    daemon->~CFileLogDaemon();
}

// An empty path is the config's way of saying "no file logging", not a
// mistake, so it must not be reported as a failure to open something.
TEST(logdaemon_empty_path_is_logging_disabled)
{
    ResetLogging();

    CFileLogDaemon daemon("", 5);
    CHECK(!daemon.IsFileLogging());
    CHECK_EQ(daemon.GetOpenResult(), FR_INVALID_NAME);

    ResetLogging();
    QueueEvents(16);
    daemon.DrainOnce();
    CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
    CHECK_EQ(CScheduler::TestSleepCount(), 0u);
}

// The path that has to keep working. CLogger queues every event regardless of
// level, so the configured level has to be applied on this side of the queue.
TEST(logdaemon_writes_and_applies_the_configured_level)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/logdaemon.txt";
    RemoveFile(path);

    {
        // Level 3 keeps panic(0), error(1) and warning(2); notice(3) and
        // debug(4) are dropped.
        CFileLogDaemon daemon(path.c_str(), 3);
        CHECK(daemon.IsFileLogging());
        CHECK_EQ(daemon.GetOpenResult(), FR_OK);

        ResetLogging();
        CLogger::TestQueueEvent(LogError, "src", "an error");
        CLogger::TestQueueEvent(LogWarning, "src", "a warning");
        CLogger::TestQueueEvent(LogNotice, "src", "a notice");
        CLogger::TestQueueEvent(LogDebug, "src", "a debug line");
        daemon.DrainOnce();

        CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
        CHECK_EQ(CScheduler::TestSleepCount(), 0u);
    }

    const std::string contents = ReadWholeFile(path);
    CHECK(contents.find("an error") != std::string::npos);
    CHECK(contents.find("a warning") != std::string::npos);
    CHECK(contents.find("a notice") == std::string::npos);
    CHECK(contents.find("a debug line") == std::string::npos);

    RemoveFile(path);
}

// The daemon kept the caller's pointer rather than the string, and the config
// store replaces that value when the web UI edits the log path.
TEST(logdaemon_keeps_its_own_copy_of_the_path)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/logdaemon-copy.txt";
    RemoveFile(path);

    char caller[256];
    strncpy(caller, path.c_str(), sizeof(caller) - 1);
    caller[sizeof(caller) - 1] = '\0';

    {
        CFileLogDaemon daemon(caller, 5);
        CHECK(daemon.IsFileLogging());

        // The config store hands out a new value for the key.
        memset(caller, 0, sizeof(caller));
        strncpy(caller, "0:/somewhere-else.txt", sizeof(caller) - 1);

        CHECK(strcmp(daemon.GetLogFilePath(), path.c_str()) == 0);

        ResetLogging();
        CLogger::TestQueueEvent(LogError, "src", "still the original file");
        daemon.DrainOnce();
        CHECK_EQ(CScheduler::TestSleepCount(), 0u);
    }

    const std::string contents = ReadWholeFile(path);
    CHECK(contents.find("still the original file") != std::string::npos);

    RemoveFile(path);
}
