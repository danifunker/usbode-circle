//
// A CDROM Emulator Service
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
#include "cdromservice.h"

#include <assert.h>
#include <circle/new.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/logger.h>

//TODO reduce stack size of USBCDGadget
#define CDROM_STACK_SIZE TASK_STACK_SIZE * 1.5

LOGMODULE("cdrom");

CDROMService *CDROMService::s_pThis = nullptr;

CDROMService::CDROMService()
: CTask (CDROM_STACK_SIZE)
{
      
    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    LOGNOTE("CDROM starting");
    SetName("cdromservice");
    boolean ok = Initialize();
    assert(ok == true);
}

void CDROMService::SetDevice(IImageDevice* pBinFileDevice) {
    LOGNOTE("CDROM setting device");
    
    // We defer initialization of the CD Gadget until the first CD image is loaded
    if (!isInitialized) {
        // First load: Initialize USB BEFORE setting device
        bool ok = m_CDGadget->Initialize();
        assert(ok && "Failed to initialize CD Gadget");
        LOGNOTE("Initialized USB CD gadget");
        isInitialized = true;
        
        // Give USB enumeration a moment to complete
        // This ensures OnActivate() has been called before we set the device
        CScheduler::Get()->MsSleep(100);
    }
    
    // Now set the device (will be handled correctly by OnActivate or as disc swap)
    m_CDGadget->SetDevice(pBinFileDevice);
}

boolean CDROMService::Initialize() {
    LOGNOTE("CDROM Initializing");
    CInterruptSystem* m_Interrupt = CInterruptSystem::Get();
    m_CDGadget = new CUSBCDGadget(m_Interrupt, CKernelOptions::Get()->GetUSBFullSpeed());
    LOGNOTE("Started USB CD gadget");
    return true;
}

CDROMService::~CDROMService(void) {
    s_pThis = nullptr;
}

void CDROMService::Run(void) {
    LOGNOTE("CDROM Run Loop entered");

    while (true) {
	    m_CDGadget->UpdatePlugAndPlay();
            m_CDGadget->Update();
	    CScheduler::Get()->Yield();
    }

}
