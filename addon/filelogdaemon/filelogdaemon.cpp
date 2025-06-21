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

CFileLogDaemon *CFileLogDaemon::s_pThis = 0;

CFileLogDaemon::CFileLogDaemon(const char *pLogFilePath)
    : m_pLogFilePath(pLogFilePath) {
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    SetName(FromFileLogDaemon);
    Initialize();
}

boolean CFileLogDaemon::Initialize() {
    // Open log file for writing (append mode)
    FRESULT Result = f_open(&m_LogFile, m_pLogFilePath, FA_WRITE | FA_OPEN_ALWAYS);
    if (Result != FR_OK) {
        LOGERR("Failed to open log file");
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

CFileLogDaemon::~CFileLogDaemon(void) {
    s_pThis = 0;

    if (m_bFileInitialized)
        f_close(&m_LogFile);
}

void CFileLogDaemon::Run(void) {
    CLogger *pLogger = CLogger::Get();
    assert(pLogger != 0);

    // Register ourselves as the notification handler
    pLogger->RegisterEventNotificationHandler(EventNotificationHandler);
    pLogger->RegisterPanicHandler(PanicHandler);

    while (true) {
        m_Event.Clear();

        TLogSeverity Severity;
        char Source[LOG_MAX_SOURCE];
        char Message[LOG_MAX_MESSAGE];
        time_t Time;
        unsigned nHundredthTime;
        int nTimeZone;
        while (pLogger->ReadEvent(&Severity, Source, Message,
                                  &Time, &nHundredthTime, &nTimeZone)) {
            if (!LogMessage(Severity, Time, nHundredthTime, nTimeZone, Source, Message)) {
                CScheduler::Get()->Sleep(20);
            }
        }

        m_Event.Wait();
    }
}

boolean CFileLogDaemon::LogMessage(TLogSeverity Severity,
                                   time_t FullTime, unsigned nPartialTime, int nTimeNumOffset,
                                   const char *pAppName, const char *pMsg) {
    if (!m_bFileInitialized) {
        return FALSE;
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

    // Write to file
    UINT BytesWritten;
    FRESULT Result = f_write(&m_LogFile, LogEntry, strlen(LogEntry), &BytesWritten);
    if (Result != FR_OK) {
        // TODO implement proper error handling here!!!
        LOGERR("Failed to write to log file!");
        return FALSE;
    }

    f_sync(&m_LogFile);

    return TRUE;
}

void CFileLogDaemon::EventNotificationHandler(void) {
    s_pThis->m_Event.Set();
}

void CFileLogDaemon::PanicHandler(void) {
    EnableIRQs();  // may be called on IRQ_LEVEL, where we cannot sleep

    CScheduler::Get()->Sleep(5);
}
