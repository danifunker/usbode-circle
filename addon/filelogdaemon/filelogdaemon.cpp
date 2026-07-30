//
// A file log daemon, based on syslogdaemon.cpp
//
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2025 Ian Cass - bundled for USBODE
// Copyright (C) 2020-2021  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "filelogdaemon.h"

#include <assert.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>

static const char FromFileLogDaemon[] = "filelogd";
LOGMODULE("filelogdaemon");

CFileLogDaemon *CFileLogDaemon::s_pThis = nullptr;

CFileLogDaemon::CFileLogDaemon(const char *pLogFilePath, unsigned uiLogLevel)
    : m_uiLogLevel(uiLogLevel > 5 ? 5 : uiLogLevel) {
    if (pLogFilePath != nullptr) {
        strncpy(m_LogFilePath, pLogFilePath, sizeof(m_LogFilePath) - 1);
        m_LogFilePath[sizeof(m_LogFilePath) - 1] = '\0';
    }
    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    SetName(FromFileLogDaemon);
    Initialize();
}

CFileLogDaemon *CFileLogDaemon::Get(void) {
    return s_pThis;
}

void CFileLogDaemon::SetLogLevel(unsigned uiLogLevel) {
    m_uiLogLevel = uiLogLevel > 5 ? 5 : uiLogLevel;
}

boolean CFileLogDaemon::Initialize() {
    if (m_LogFilePath[0] == '\0') {
        // An empty path is the config saying "no file logging", not an error.
        m_OpenResult = FR_INVALID_NAME;
        LOGNOTE("No log file configured; file logging is off");
        return FALSE;
    }

    // Open log file for writing (append mode)
    FRESULT Result = f_open(&m_LogFile, m_LogFilePath, FA_WRITE | FA_OPEN_ALWAYS);
    m_OpenResult = Result;
    if (Result != FR_OK) {
        // Name the path and the reason; this is the only evidence the user gets.
        // Only volume 0: is mounted this early, so a path on 1: lands here.
        LOGERR("Failed to open log file '%s' (FatFs error %d); file logging is off",
               m_LogFilePath, (int)Result);
        return FALSE;
    }

    // Seek to end of file to append
    f_lseek(&m_LogFile, f_size(&m_LogFile));

    // Attempt to write header
    const char *Header = "\n--- New Session Started ---\n";
    UINT BytesWritten;
    Result = f_write(&m_LogFile, Header, strlen(Header), &BytesWritten);
    if (Result != FR_OK || BytesWritten != strlen(Header)) {
        LOGERR("Failed to write header to log file");
        f_close(&m_LogFile);
        return FALSE;
    }
    f_sync(&m_LogFile);

    // All good!
    LOGNOTE("Enhanced logger initialized successfully");
    m_bFileInitialized = TRUE;
    return TRUE;
}

void CFileLogDaemon::GetStatusText(char *pBuffer, size_t nBufferSize) const {
    if (pBuffer == nullptr || nBufferSize == 0) {
        return;
    }

    if (m_LogFilePath[0] == '\0') {
        snprintf(pBuffer, nBufferSize, "File logging is off (no log file configured).");
    } else if (m_bWritesGaveUp) {
        snprintf(pBuffer, nBufferSize,
                 "NOT LOGGING: writes to %s kept failing, so logging stopped. "
                 "The card is most likely full.",
                 m_LogFilePath);
    } else if (m_bFileInitialized) {
        snprintf(pBuffer, nBufferSize, "Writing to %s", m_LogFilePath);
    } else {
        snprintf(pBuffer, nBufferSize,
                 "NOT LOGGING: cannot open %s (FatFs error %d). "
                 "The file must be on the boot partition and its directory must already exist.",
                 m_LogFilePath, (int)m_OpenResult);
    }
}

void CFileLogDaemon::GetStdioPath(char *pBuffer, size_t nBufferSize) const {
    if (pBuffer == nullptr || nBufferSize == 0) {
        return;
    }

    // Config stores the FatFs form ("0:/usbode-log.txt"); newlib wants an
    // ordinary path ("/usbode-log.txt").
    const char *p = m_LogFilePath;
    if (p[0] == '0' && p[1] == ':') {
        p += 2;
    }
    snprintf(pBuffer, nBufferSize, "%s", p);
}

CFileLogDaemon::~CFileLogDaemon(void) {
    s_pThis = nullptr;

    if (m_bFileInitialized)
        f_close(&m_LogFile);
}

void CFileLogDaemon::Run(void) {
    CLogger *pLogger = CLogger::Get();
    assert(pLogger != nullptr);

    // Register ourselves as the notification handler
    pLogger->RegisterEventNotificationHandler(EventNotificationHandler);
    pLogger->RegisterPanicHandler(PanicHandler);

    while (true) {
        m_Event.Clear();
        DrainOnce();
        m_Event.Wait();
    }
}

void CFileLogDaemon::DrainOnce(void) {
    CLogger *pLogger = CLogger::Get();
    assert(pLogger != nullptr);

    TLogSeverity Severity;
    char Source[LOG_MAX_SOURCE];
    char Message[LOG_MAX_MESSAGE];
    time_t Time;
    unsigned nHundredthTime;
    int nTimeZone;
    while (pLogger->ReadEvent(&Severity, Source, Message,
                              &Time, &nHundredthTime, &nTimeZone)) {
        // CLogger queues every event whatever the loglevel, so the configured
        // level applies here. LogPanic(0)..LogDebug(4) maps to config 1..5.
        if ((unsigned)Severity >= m_uiLogLevel) {
            continue;
        }

        const LogResult result =
            LogMessage(Severity, Time, nHundredthTime, nTimeZone, Source, Message);

        if (result == LogResult::Written) {
            m_nConsecutiveWriteFailures = 0;
            continue;
        }
        if (result != LogResult::WriteFailed) {
            continue;
        }

        // Back off for a busy card, but a full one fails every write, and 20 ms
        // per event starves the scheduler exactly as the no-file case used to.
        if (++m_nConsecutiveWriteFailures >= MaxConsecutiveWriteFailures) {
            if (!m_bWritesGaveUp) {
                m_bWritesGaveUp = TRUE;
                m_bFileInitialized = FALSE;
                f_close(&m_LogFile);
            }
            continue;
        }

        CScheduler::Get()->Sleep(20);
    }
}

CFileLogDaemon::LogResult CFileLogDaemon::LogMessage(TLogSeverity Severity,
                                                     time_t FullTime, unsigned nPartialTime,
                                                     int nTimeNumOffset,
                                                     const char *pAppName, const char *pMsg) {
    if (!m_bFileInitialized) {
        return LogResult::NoFile;
    }

    // Format the log entry similar to base logger but tailored for file
    const char *pSeverityName = "???";
    switch (Severity) {
        case LogPanic:
            pSeverityName = "PANIC";
            break;
        case LogError:
            pSeverityName = "ERROR";
            break;
        case LogWarning:
            pSeverityName = "WARNING";
            break;
        case LogNotice:
            pSeverityName = "NOTICE";
            break;
        case LogDebug:
            pSeverityName = "DEBUG";
            break;
        default:
            pSeverityName = "UNKNOWN";
            break;
    }

    // Create the log entry with a timestamp - prepare it before file operations
    char LogEntry[512];

    snprintf(LogEntry, sizeof(LogEntry), "[%lu] [%s] %s: %s\n",
             FullTime, pAppName, pSeverityName, pMsg);

    // Write to file. A short write means a full card, which f_write reports as
    // success, and an unchecked f_sync would then call the lost entry written.
    const UINT EntryLength = strlen(LogEntry);
    UINT BytesWritten;
    FRESULT Result = f_write(&m_LogFile, LogEntry, EntryLength, &BytesWritten);
    if (Result != FR_OK || BytesWritten != EntryLength) {
        // Not logged: this runs while draining the log queue, so a message here
        // would queue another event that fails the same way.
        return LogResult::WriteFailed;
    }

    if (f_sync(&m_LogFile) != FR_OK) {
        return LogResult::WriteFailed;
    }

    return LogResult::Written;
}

void CFileLogDaemon::EventNotificationHandler(void) {
    s_pThis->m_Event.Set();
}

void CFileLogDaemon::PanicHandler(void) {
    EnableIRQs();  // may be called on IRQ_LEVEL, where we cannot sleep

    CScheduler::Get()->Sleep(5);
}
