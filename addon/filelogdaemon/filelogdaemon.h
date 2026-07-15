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

    // Takes effect immediately; only affects the log file, not the
    // loglevel= filtering Circle applies to the serial/screen target.
    void SetLogLevel(unsigned uiLogLevel);

    static CFileLogDaemon *Get(void);

   private:
    boolean LogMessage(TLogSeverity Severity,
                       time_t FullTime, unsigned nPartialTime, int nTimeNumOffset,
                       const char *pAppName, const char *pMsg);

    static void EventNotificationHandler(void);
    static void PanicHandler(void);

   private:
    CSynchronizationEvent m_Event;
    static CFileLogDaemon *s_pThis;
    boolean m_bFileInitialized;
    const char *m_pLogFilePath;
    unsigned m_uiLogLevel;
    FIL m_LogFile;
};

#endif
