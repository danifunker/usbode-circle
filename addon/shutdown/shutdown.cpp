//
// A Media player
//
//
// Circle - A C++ bare metal environment for Raspberry Pi
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

    SetName("shutdown");
    LOGNOTE("Shutdown scheduler called (%d scheduled in %dms)", mode, msdelay);
}

CShutdown::~CShutdown(void) {
}

void CShutdown::Run(void) {
	LOGNOTE("Sleeping for %d ms", m_msdelay);
	CScheduler::Get()->MsSleep(m_msdelay);
	DeviceState::Get().setShutdownMode(ShutdownReboot);
}
