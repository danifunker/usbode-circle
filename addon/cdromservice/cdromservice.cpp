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

// TODO reduce stack size of USBCDGadget
#define CDROM_STACK_SIZE TASK_STACK_SIZE * 1.5

LOGMODULE("cdrom");

CDROMService *CDROMService::s_pThis = nullptr;

CDROMService::CDROMService(u16 vid, u16 pid)
    : CTask(CDROM_STACK_SIZE), m_vid(vid), m_pid(pid)
{
    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    LOGNOTE("CDROMService constructor: VID=0x%04x PID=0x%04x Protocol=%d", vid, pid);
    SetName("cdromservice");
    boolean ok = Initialize();
    assert(ok == true);
}

// In cdromservice.cpp
void CDROMService::SetDevice(IImageDevice *pDevice)
{
    LOGNOTE("CDROM setting device (type: %d)", (int)pDevice->GetFileType());

    if (pDevice->HasSubchannelData())
    {
        LOGNOTE("Device has subchannel data");
    }

    // Set device FIRST - this arms the image without USB activity
    m_CDGadget->SetDevice(pDevice);

    // NOW initialize USB hardware on first device load
    // The gadget will wait for host reset before enumerating
    if (!isInitialized)
    {
        LOGNOTE("Image loaded - activating USB hardware");
        bool ok = m_CDGadget->Initialize();
        assert(ok && "Failed to initialize CD Gadget");
        LOGNOTE("USB hardware active - device will enumerate when host connects");
        isInitialized = true;

        CScheduler::Get()->MsSleep(100);
    }
    else
    {
        LOGNOTE("USB already active - disc swap ready");
    }
}

boolean CDROMService::Initialize()
{
    LOGNOTE("CDROM Initializing");
    CInterruptSystem *m_Interrupt = CInterruptSystem::Get();
    
    // Pass VID/PID directly to constructor - no separate config step needed
    m_CDGadget = new CUSBCDGadget(
        m_Interrupt, 
        CKernelOptions::Get()->GetUSBFullSpeed(),
        nullptr,  // pDevice - will be set later via SetDevice()
        m_vid,    // USB Vendor ID
        m_pid     // USB Product ID
    );
    
    LOGNOTE("Created USB CD gadget with VID: 0x%04x PID: 0x%04x", m_vid, m_pid);
    return true;
}

CDROMService::~CDROMService(void)
{
    s_pThis = nullptr;
}

void CDROMService::Run(void)
{
    LOGNOTE("CDROM Run Loop entered");

    while (true)
    {
        m_CDGadget->UpdatePlugAndPlay();
        m_CDGadget->Update();
        CScheduler::Get()->Yield();
    }
}
