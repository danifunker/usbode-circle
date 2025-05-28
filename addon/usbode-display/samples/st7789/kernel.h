//
// kernel.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2021  R. Stange <rsta2@o2online.de>
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

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/spimaster.h>
#include <circle/types.h>
#include <usbode-display/st7789display.h>
#include <circle/chargenerator.h>
#include <circle/2dgraphics.h> // Add this include

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

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

	CSPIMaster		m_SPIMaster;
	CST7789Display		m_Display;
	C2DGraphics         m_Graphics;   // Add this line

    // Button GPIO pins for Pirate Audio
    static const unsigned BUTTON_A_PIN = 5;  // Up button
    static const unsigned BUTTON_B_PIN = 6;  // Down button
    static const unsigned BUTTON_X_PIN = 16; // Cancel button
    static const unsigned BUTTON_Y_PIN = 24; // Select button
    
    // GPIO pins for buttons
    CGPIOPin m_ButtonA;
    CGPIOPin m_ButtonB;
    CGPIOPin m_ButtonX;
    CGPIOPin m_ButtonY;
    
    // Track previous button states
    boolean m_bLastButtonAState;
    boolean m_bLastButtonBState;
    boolean m_bLastButtonXState;
    boolean m_bLastButtonYState;
    
    // Helper method to display button press
    void DisplayButtonPress(const char *pButtonName);
};

// Different font sizes for better UI
extern TFont LargeFont;  // Large font for headings
extern TFont MediumFont; // Medium font for normal text
extern TFont SmallFont;  // Small font for details

#endif
