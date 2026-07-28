//
// syslogdaemon.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2017  R. Stange <rsta2@o2online.de>
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
#ifndef _circle_net_syslogdaemon_h
#define _circle_net_syslogdaemon_h

#include <circle/logger.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/sched/task.h>
#include <circle/time.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>

#define SYSLOG_VERSION 1
#define SYSLOG_PORT 514

class CFileLogDaemon : public CTask {
   public:
    // uiLogLevel uses the USBODE config scale (0=no logging, 1=panic only,
    // 2=+errors, 3=+warnings, 4=+notices, 5=+debug). This is one more than
    // Circle's TLogSeverity, whose scale has no "off" value.
    CFileLogDaemon(const char *pLogFilePath, unsigned uiLogLevel = 4);
    ~CFileLogDaemon(void);
    boolean Initialize();
    void Run(void);

    /// One pass of Run()'s loop: drain whatever the logger has queued. Split
    /// out because Run() never returns, and the drain is the part with the
    /// interesting behaviour when the log file could not be opened.
    void DrainOnce(void);

    /// Whether the log file is actually open. The constructor cannot report a
    /// failure, so the caller has to be able to ask afterwards - otherwise a
    /// mistyped path silently produces a system with no file logging at all.
    boolean IsFileLogging(void) const { return m_bFileInitialized; }

    /// The path this daemon was asked for, for a caller that wants to name it
    /// in a diagnostic.
    const char *GetLogFilePath(void) const { return m_LogFilePath; }

    /// The FatFs result from the attempt to open it. FR_OK once open.
    FRESULT GetOpenResult(void) const { return m_OpenResult; }

    /// One line describing whether file logging is actually working, for the
    /// web UI to show.
    ///
    /// Without this the only report of a failed open is a warning on the
    /// serial console, and SCREEN_HEADLESS builds have nowhere else to put it
    /// - so the user sees a device with no log file and no reason given, which
    /// is the situation this whole fix exists to end.
    void GetStatusText(char *pBuffer, size_t nBufferSize) const;

    /// The configured path as an ordinary filesystem path (no "0:" volume
    /// prefix), for callers that open it through newlib rather than FatFs.
    void GetStdioPath(char *pBuffer, size_t nBufferSize) const;

    // Takes effect immediately; only affects the log file, not the
    // loglevel= filtering Circle applies to the serial/screen target.
    void SetLogLevel(unsigned uiLogLevel);

    static CFileLogDaemon *Get(void);

   private:
    /// Why a message did not reach the file. The distinction matters: a write
    /// that failed may well succeed next time and is worth backing off for,
    /// but a file that was never opened will never take a message, and backing
    /// off for each of those costs the whole system (see Run()).
    enum class LogResult
    {
        Written,
        WriteFailed,  // transient: the file is open, this write did not land
        NoFile        // permanent for this boot: there is nothing to write to
    };

    LogResult LogMessage(TLogSeverity Severity,
                         time_t FullTime, unsigned nPartialTime, int nTimeNumOffset,
                         const char *pAppName, const char *pMsg);

    static void EventNotificationHandler(void);
    static void PanicHandler(void);

   private:
    CSynchronizationEvent m_Event;
    static CFileLogDaemon *s_pThis;
    /// Must start FALSE. An indeterminate value here let LogMessage() write to
    /// an unopened FIL and the destructor close it, whenever the open failed.
    boolean m_bFileInitialized = FALSE;
    /// Copied, not aliased. The caller's string comes out of the config store,
    /// which is free to replace it while this daemon is still running.
    char m_LogFilePath[256] = {0};
    FRESULT m_OpenResult = FR_NOT_READY;
    unsigned m_uiLogLevel;
    FIL m_LogFile;
};

#endif
