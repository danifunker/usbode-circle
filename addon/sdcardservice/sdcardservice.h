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
#ifndef _ssdcardservice_h
#define _ssdcardservice_h

#include <circle/machineinfo.h>
#include <circle/sched/task.h>
#include <circle/new.h>
#include <circle/time.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/koptions.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>
#include <usbmsdgadget/usbmsdgadget.h>

class SDCARDService : public CTask {
   public:
    SDCARDService(CDevice *pDevice);
    ~SDCARDService(void);
    boolean Initialize();
    void Run(void);

   private:
   private:
    CDevice *m_pDevice;
    CUSBMMSDGadget* m_MSDGadget = nullptr;
    static SDCARDService *s_pThis;
    bool isInitialized = false;
};

#endif
