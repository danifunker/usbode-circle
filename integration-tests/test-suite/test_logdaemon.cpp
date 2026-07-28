//
// test_logdaemon.cpp
//
// The file log daemon (addon/filelogdaemon/filelogdaemon.cpp), driven on the
// host through the same FatFs seam the disc-image readers use.
//
// The interesting behaviour is not the happy path. A user reported a Pi that
// had gone slow and had no log file, from a log path of
// "0:/SD:/usbode-logs.txt" - a value the web form built out of what he typed.
// Every one of these tests is about what the daemon does when the file it was
// told to open is not there to open.
//
// Run() never returns, so the tests drive DrainOnce(), which is one pass of
// its loop.
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

// Every test starts from a clean queue: the firmware sources the rest of the
// suite exercises log freely, and those events sit in the same queue.
static void ResetLogging()
{
    CLogger::TestClearEvents();
    CScheduler::TestResetSleepCount();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The headline defect. With no file open every message fails, and the drain
// loop used to sleep 20 ms for each one - so the log queue retired 50 events a
// second and took the scheduler down with it. The daemon still has to consume
// the events (leaving them queued would strand the logger), it just must not
// pay for them.
TEST(logdaemon_unopenable_path_drains_without_sleeping)
{
    ResetLogging();

    // A directory that does not exist, which is the shape of the reported
    // value: "SD:/usbode-logs.txt" with "0:/" pasted on the front.
    const std::string path = TestDataDir() + "/no-such-dir/usbode-logs.txt";

    CFileLogDaemon daemon(path.c_str(), 5);
    CHECK(!daemon.IsFileLogging());
    CHECK(daemon.GetOpenResult() != FR_OK);

    // Constructing the daemon logs its own failure, which queues an event.
    // Clear so the count below is only what this test put there.
    ResetLogging();

    const unsigned kEvents = 200;
    QueueEvents(kEvents);
    CHECK_EQ(CLogger::TestQueuedEventCount(), kEvents);

    daemon.DrainOnce();

    // Consumed, and for free.
    CHECK_EQ(CLogger::TestQueuedEventCount(), 0u);
    CHECK_EQ(CScheduler::TestSleepCount(), 0u);
}

// m_bFileInitialized had no initializer and was not in the constructor's init
// list, so when the open failed it held whatever was already in that memory.
// Non-zero meant LogMessage() wrote to an unopened FIL and the destructor
// closed one.
//
// Constructing over poisoned storage is what exposes that. Note what actually
// catches it, though: the flag is a bool, and reading a bool that holds
// neither 0 nor 1 is undefined, so an ordinary CHECK cannot be relied on - the
// optimizer is free to fold the test either way, and at -O1 it does. The pin
// is the sanitizer build:
//
//   make -C integration-tests clean
//   make -C integration-tests \
//       CXXFLAGS="-std=c++17 -O0 -g -fsanitize=address,undefined" \
//       CFLAGS="-O0 -g -fsanitize=address,undefined" \
//       LDFLAGS="-fsanitize=address,undefined"
//
// Remove the initializer and that run reports "load of value 248, which is not
// a valid value for type 'boolean'" at all three places the flag is read: the
// IsFileLogging() accessor, LogMessage()'s guard, and the destructor's decision
// to close the file. What is asserted below is the behaviour that follows from
// the flag being false - nothing written, nothing closed, no back-off.
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

// The path that has to keep working: a usable file gets the messages, with the
// configured level applied on this side of the queue (CLogger queues
// everything regardless of level, so a daemon that relied on the queue to
// filter would log too much).
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

// The daemon kept the caller's pointer rather than the string. It came from
// the config store, which is free to replace the value while the daemon is
// still running - and the web UI does exactly that when the log path is
// edited. Overwrite the caller's buffer and the daemon must be unaffected.
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
