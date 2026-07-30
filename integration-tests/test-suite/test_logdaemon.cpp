//
// test_logdaemon.cpp
//
// The file log daemon on the host FatFs seam, mostly about what it does when the
// file it was told to open is not there. Run() never returns, so the tests drive
// DrainOnce(), one pass of its loop.
//
#include "framework.h"
#include "fatfs_host.h"

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

// With no file open the drain loop slept 20 ms per message, retiring 50 events a
// second and taking the scheduler with it. They must be consumed, not paid for.
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

// m_bFileInitialized had no initializer, so a failed open left it holding stale
// memory. Asserts the behaviour, not the flag: reading an invalid bool is UB.
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

// What the web UI shows. The boot warning goes to a serial console SCREEN_HEADLESS
// does not have, so a bad path otherwise looks like a device that just works.
TEST(logdaemon_reports_its_status_for_the_web_ui)
{
    ResetLogging();
    char status[256];

    // Working.
    const std::string good = TestDataDir() + "/logdaemon-status.txt";
    RemoveFile(good);
    {
        CFileLogDaemon daemon(good.c_str(), 5);
        CHECK(daemon.IsFileLogging());
        daemon.GetStatusText(status, sizeof(status));
        CHECK(strstr(status, "Writing to") != nullptr);
        CHECK(strstr(status, "NOT LOGGING") == nullptr);
    }
    RemoveFile(good);

    // Broken: must name the path and say it is not logging.
    ResetLogging();
    const std::string bad = TestDataDir() + "/no-such-dir/usbode-logs.txt";
    {
        CFileLogDaemon daemon(bad.c_str(), 5);
        CHECK(!daemon.IsFileLogging());
        daemon.GetStatusText(status, sizeof(status));
        CHECK(strstr(status, "NOT LOGGING") != nullptr);
        CHECK(strstr(status, "usbode-logs.txt") != nullptr);
    }

    // Off on purpose is not an error and must not read like one.
    ResetLogging();
    {
        CFileLogDaemon daemon("", 5);
        daemon.GetStatusText(status, sizeof(status));
        CHECK(strstr(status, "off") != nullptr);
        CHECK(strstr(status, "NOT LOGGING") == nullptr);
    }

    // The log viewer opens the file through newlib, which wants an ordinary path
    // rather than the FatFs volume form the config stores.
    ResetLogging();
    {
        CFileLogDaemon daemon("0:/somewhere/usbode-log.txt", 5);
        char path[256];
        daemon.GetStdioPath(path, sizeof(path));
        CHECK(strcmp(path, "/somewhere/usbode-log.txt") == 0);
    }
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

// A full card is the write failure this daemon will actually meet, and FatFs
// reports it as FR_OK with a short count, so the FRESULT alone is not enough.
TEST(logdaemon_treats_a_short_write_as_a_failure)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/logdaemon-fullcard.txt";
    RemoveFile(path);

    {
        CFileLogDaemon daemon(path.c_str(), 5);
        CHECK(daemon.IsFileLogging());

        // Room for part of one entry and no more.
        ResetLogging();
        FatFsHostSetWriteLimit(8);

        QueueEvents(3);
        daemon.DrainOnce();

        FatFsHostClearFaults();

        CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
        // The 20 ms back-off is the only outward sign the daemon noticed.
        CHECK(CScheduler::TestSleepCount() > 0u);
    }

    RemoveFile(path);
}

// A full card fails every write, so 20 ms per event is the same starvation an
// unopenable path used to cause. It gives up on the file instead, and says so.
TEST(logdaemon_stops_writing_when_the_card_stays_full)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/logdaemon-givesup.txt";
    RemoveFile(path);

    {
        CFileLogDaemon daemon(path.c_str(), 5);
        CHECK(daemon.IsFileLogging());

        ResetLogging();
        FatFsHostSetWriteLimit(4);

        QueueEvents(60);
        daemon.DrainOnce();

        FatFsHostClearFaults();

        CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
        // Bounded, not one per event.
        CHECK(CScheduler::TestSleepCount() > 0u);
        CHECK(CScheduler::TestSleepCount() <= 8u);

        CHECK(!daemon.IsFileLogging());
        char status[256];
        daemon.GetStatusText(status, sizeof(status));
        CHECK(strstr(status, "NOT LOGGING") != nullptr);
        CHECK(strstr(status, "logdaemon-givesup.txt") != nullptr);
    }

    RemoveFile(path);
}

// f_sync() is the other half: f_write() can report every byte taken and the
// entry still be lost when the flush fails.
TEST(logdaemon_treats_a_failed_sync_as_a_failure)
{
    ResetLogging();

    const std::string path = TestDataDir() + "/logdaemon-badsync.txt";
    RemoveFile(path);

    {
        CFileLogDaemon daemon(path.c_str(), 5);
        CHECK(daemon.IsFileLogging());

        ResetLogging();
        FatFsHostFailSync(true);

        QueueEvents(2);
        daemon.DrainOnce();

        FatFsHostClearFaults();

        CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
        CHECK(CScheduler::TestSleepCount() > 0u);
    }

    RemoveFile(path);
}
