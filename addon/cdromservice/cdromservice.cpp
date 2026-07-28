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

void CDROMService::SetDevice(IImageDevice *pDevice)
{ // Changed from ICueDevice*
    LOGNOTE("CDROM setting device (type: %d)", (int)pDevice->GetFileType());

    // Log if this device has subchannel support
    if (pDevice->HasSubchannelData())
    {
        LOGNOTE("Device has subchannel data");
    }

    // We defer initialization of the CD Gadget until the first CD image is loaded.
    //
    // LOAD-BEARING, not just an optimization. This is the only path that brings
    // the USB gadget up, so with no image there are no endpoints at all - and
    // the QEMU boot test depends on exactly that, because QEMU cannot emulate
    // dwc2 device mode. It keeps the images partition empty so this moment
    // never arrives, and validate_boot_log.py lists the symptoms of the
    // hardware path running - "does not support USB gadget mode", "dwgadget:
    // Unknown vendor", "Failed to initialize CD Gadget" - as FORBIDDEN_ANYWHERE.
    // See tests/qemu-boot/README.md.
    //
    // So: do not move initialization earlier, and do not make it unconditional.
    // Note that ejected=1 does NOT avoid it either - the boot path still
    // pre-loads the remembered image, and ArmBootEject() only hides the medium
    // from the host after the gadget is already up.
    if (!isInitialized)
    {
        bool ok = m_CDGadget->Initialize();
        assert(ok && "Failed to initialize CD Gadget");
        LOGNOTE("Initialized USB CD gadget");
        isInitialized = true;

        CScheduler::Get()->MsSleep(100);
    }

    m_CDGadget->SetDevice(pDevice);
}

void CDROMService::Eject(void)
{
    if (m_CDGadget)
        m_CDGadget->Eject();
}

void CDROMService::Insert(void)
{
    if (m_CDGadget)
        m_CDGadget->Insert();
}

bool CDROMService::IsEjected(void) const
{
    return m_CDGadget && m_CDGadget->IsEjected();
}

void CDROMService::ArmBootEject(void)
{
    if (m_CDGadget)
        m_CDGadget->ArmBootEject();
}

void CDROMService::DisarmBootEject(void)
{
    if (m_CDGadget)
        m_CDGadget->DisarmBootEject();
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
