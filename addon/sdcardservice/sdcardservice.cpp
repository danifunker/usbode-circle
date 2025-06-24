//
// An SD Card Service
//
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
#include "sdcardservice.h"

#include <assert.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/logger.h>

#define SDCARD_STACK_SIZE TASK_STACK_SIZE

LOGMODULE("sdcard");

SDCARDService *SDCARDService::s_pThis = 0;

SDCARDService::SDCARDService(CDevice *pDevice)
: CTask (SDCARD_STACK_SIZE),
  m_pDevice(pDevice)
{
      
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    LOGNOTE("SDCARD starting");
    SetName("sdcardservice");
    boolean ok = Initialize();
    assert(ok == true);
}

boolean SDCARDService::Initialize() {
    LOGNOTE("SDCARD Initializing");
    m_MSDGadget = new CUSBMMSDGadget(CInterruptSystem::Get(), CKernelOptions::Get()->GetUSBFullSpeed(), m_pDevice);
    if (!m_MSDGadget->Initialize()) {
        LOGERR("Failed to initialize USB MSD gadget");
        return false;
    }
    LOGNOTE("Started USB MSD gadget");
    return true;
}

SDCARDService::~SDCARDService(void) {
    s_pThis = 0;
}

void SDCARDService::Run(void) {
    LOGNOTE("SDCARD Run Loop entered");

    while (true) {
	    m_MSDGadget->UpdatePlugAndPlay();
            m_MSDGadget->Update();
	    CScheduler::Get()->Yield();
    }

}
