//
// kernel.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
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
#ifndef _kernel_h
#define _kernel_h

#include <Properties/propertiesfatfsfile.h>
#include <SDCard/emmc.h>
#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/net/mdnspublisher.h>
#include <circle/net/netsubsystem.h>
#include <circle/sched/scheduler.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/usb/gadget/usbcdgadget.h>
#include <discimage/cuebinfile.h>
#include <fatfs/ff.h>
#include <filelogdaemon/filelogdaemon.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include "displaymanager.h"
#include "gpiobuttonmanager.h"

enum TShutdownMode {
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel 
{
   public:
    CKernel(void);
    ~CKernel(void);

    boolean Initialize(void);
    boolean SetDevice(char *imageName);

    TShutdownMode Run(void);

public:
    // Static callback for display updates (must be public to be used as a callback)
    static void DisplayUpdateCallback(const char* imageName);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CScheduler              m_Scheduler;

	CEMMCDevice		m_EMMC;
	FATFS                   m_FileSystem;
	CBcm4343Device          m_WLAN;
        CNetSubSystem           m_Net;
        CWPASupplicant          m_WPASupplicant;
	CUSBCDGadget		m_CDGadget;

	// SPI and display components
	CSPIMaster* m_pSPIMaster;
	CDisplayManager* m_pDisplayManager;

	// GPIO button manager
	CGPIOButtonManager* m_pButtonManager;

	// Helper method to parse display type from config.txt
	TDisplayType ParseDisplayType(void);

	// Helper method for display initialization
	TDisplayType GetDisplayTypeFromString(const char* displayType);
	void InitializeDisplay(TDisplayType displayType);  // Change parameter type from const char* to TDisplayType

	// Flag to track USB initialization state
	boolean m_bUSBInitialized;

	// Config option name
	static const char ConfigOptionDisplayType[];

	// Updates the display with current status information
	void UpdateDisplayStatus(const char* imageName);

	// Button event callback
	static void ButtonEventHandler(unsigned nButtonIndex, boolean bPressed, void* pParam);

	// Flag to indicate button test mode
	boolean m_bButtonTestMode;
};

#endif
