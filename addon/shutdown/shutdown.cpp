//
// A Scheduled shutdown service to allow parts of USBODE to schedule a
// shutdown or reboot at some point in the future, to give time for
// log file writing, web pages to return, etc
//
// Copyright (C) 2025 Ian Cass
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
#include "shutdown.h"

#include <assert.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>

LOGMODULE("shutdown");

CShutdown::CShutdown(TShutdownMode mode, int msdelay)
:	m_mode(mode),
	m_msdelay(msdelay)
{

    SetName("shutdownservice");
    LOGNOTE("Shutdown scheduler called (%d scheduled in %dms)", mode, msdelay);
}

CShutdown::~CShutdown(void) {
}

void CShutdown::Run(void) {
	LOGNOTE("Sleeping for %d ms", m_msdelay);
	CScheduler::Get()->MsSleep(m_msdelay);
	DeviceState::Get().setShutdownMode(m_mode);
}
