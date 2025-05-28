//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2024  R. Stange <rsta2@o2online.de>
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
#include "kernel.h"
#include <circle/string.h>
#include <stdint.h>
#include <usbode-display/st7789display.h>

#define WIDTH			CST7789Display::DEFAULT_WIDTH		// display width in pixels
#define HEIGHT			CST7789Display::DEFAULT_HEIGHT		// display height in pixels
#define MY_COLOR		ST7789_COLOR (31, 31, 15)	// any color

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
    m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_SPIMaster (CST7789Display::DEFAULT_SPI_CLOCK_SPEED, 
                CST7789Display::DEFAULT_SPI_CPOL, 
                CST7789Display::DEFAULT_SPI_CPHA, 
                CST7789Display::DEFAULT_SPI_MASTER_DEVICE),
    m_Display (&m_SPIMaster, 
              CST7789Display::DEFAULT_DC_PIN, 
              CST7789Display::DEFAULT_RESET_PIN, 
              CST7789Display::NONE, 
              CST7789Display::DEFAULT_WIDTH, 
              CST7789Display::DEFAULT_HEIGHT,
              CST7789Display::DEFAULT_SPI_CPOL, 
              CST7789Display::DEFAULT_SPI_CPHA, 
              CST7789Display::DEFAULT_SPI_CLOCK_SPEED, 
              CST7789Display::DEFAULT_SPI_CHIP_SELECT),
    m_Graphics (&m_Display),
    // Initialize GPIO pins as inputs with pull-up resistors
    m_ButtonA(CST7789Display::BUTTON_A_PIN, GPIOModeInputPullUp),
    m_ButtonB(CST7789Display::BUTTON_B_PIN, GPIOModeInputPullUp),
    m_ButtonX(CST7789Display::BUTTON_X_PIN, GPIOModeInputPullUp),
    m_ButtonY(CST7789Display::BUTTON_Y_PIN, GPIOModeInputPullUp),
    // Initialize last button states to HIGH (not pressed)
    m_bLastButtonAState(TRUE),
    m_bLastButtonBState(TRUE),
    m_bLastButtonXState(TRUE),
    m_bLastButtonYState(TRUE)
{
    m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_SPIMaster.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Display.Initialize ();
	}
	
	if (bOK)
	{
		bOK = m_Graphics.Initialize();
	}
	
	return bOK;
}

void CKernel::DisplayButtonPress(const char *pButtonName)
{
    // Clear the middle area of the screen
    m_Graphics.DrawRect(20, 100, 200, 50, COLOR2D(255, 255, 255));
    
    // Draw a message box
    m_Graphics.DrawRectOutline(20, 100, 200, 50, COLOR2D(0, 0, 0));
    
    // Format the message
    CString Message;
    Message.Format("Button %s pressed!", pButtonName);
    
    // Display the message
    m_Graphics.DrawText(120, 125, COLOR2D(0, 0, 0), Message, C2DGraphics::AlignCenter);
    
    // Update the display
    m_Graphics.UpdateDisplay();
    
    // Keep the message visible for 2 seconds
    m_Timer.MsDelay(2000);
    
    // Redraw the original screen
    // (You would ideally store the background and restore it)
    // This is a simple approach that clears the message
    m_Graphics.DrawRect(20, 100, 200, 50, COLOR2D(255, 255, 255));
    m_Graphics.UpdateDisplay();
}

TShutdownMode CKernel::Run (void)
{
    m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

    // Set rotation to 90 degrees for the display
    m_Display.SetRotation(270);
    
    // Clear the display first
    m_Graphics.ClearScreen(COLOR2D(255, 255, 255)); // WHITE
    
    // Draw header bar with blue background
    m_Graphics.DrawRect(0, 0, WIDTH, 30, COLOR2D(58, 124, 165));
    
    // Draw "USBODE" header text in white
    m_Graphics.DrawText(10, 5, COLOR2D(255, 255, 255), "USBODE v1.99", C2DGraphics::AlignLeft);
    
    // Draw WiFi icon
    unsigned wifi_x = 10;
    unsigned wifi_y = 40;
    
    // Draw WiFi icon - outer arc
    m_Graphics.DrawCircleOutline(wifi_x + 10, wifi_y + 10, 10, COLOR2D(0, 0, 0));
    // Draw WiFi icon - inner arc
    m_Graphics.DrawCircleOutline(wifi_x + 10, wifi_y + 10, 5, COLOR2D(0, 0, 0));
    // Center dot
    m_Graphics.DrawCircle(wifi_x + 10, wifi_y + 10, 2, COLOR2D(0, 0, 0));
    
    // Draw IP address text
    m_Graphics.DrawText(35, 40, COLOR2D(0, 0, 0), "192.168.1.100", C2DGraphics::AlignLeft);
    
    // Draw CD icon
    unsigned cd_x = 10;
    unsigned cd_y = 70;
    unsigned cd_radius = 10;
    
    // Draw outer circle of CD
    m_Graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(192, 192, 192));
    
    // Draw inner hole of CD
    m_Graphics.DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 3, COLOR2D(255, 255, 255));
    
    // Draw shine highlight - simplified diagonal line
    m_Graphics.DrawLine(cd_x + 3, cd_y + 3, cd_x + cd_radius - 3, cd_y + cd_radius - 3, COLOR2D(255, 255, 255));
    
    // Draw ISO name
    m_Graphics.DrawText(35, 70, COLOR2D(0, 0, 0), "Carmageddon.iso", C2DGraphics::AlignLeft);
    
    // Draw USB icon
    unsigned usb_x = 10;
    unsigned usb_y = 155;
    
    // Draw USB icon - horizontal line (main stem)
    m_Graphics.DrawLine(usb_x, usb_y + 8, usb_x + 20, usb_y + 8, COLOR2D(0, 0, 0));
    
    // Draw circle at left of USB icon
    m_Graphics.DrawCircleOutline(usb_x - 2, usb_y + 8, 4, COLOR2D(0, 0, 0));
    
    // Draw USB icon - top arm
    m_Graphics.DrawLine(usb_x + 6, usb_y + 8, usb_x + 6, usb_y, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(usb_x + 6, usb_y, usb_x + 14, usb_y, COLOR2D(0, 0, 0));
    
    // Draw USB icon - bottom arm
    m_Graphics.DrawLine(usb_x + 14, usb_y + 8, usb_x + 14, usb_y + 16, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(usb_x + 14, usb_y + 16, usb_x + 22, usb_y + 16, COLOR2D(0, 0, 0));
    
    // Draw mode number
    m_Graphics.DrawText(40, 155, COLOR2D(0, 0, 0), "1", C2DGraphics::AlignLeft);
    
    // Draw CD icon for mode (mode 1 = CD-ROM)
    unsigned mode_icon_x = 60;
    unsigned mode_icon_y = 155;
    unsigned mode_cd_radius = 8;
    
    // Draw outer circle of mode CD icon
    m_Graphics.DrawCircleOutline(mode_icon_x + mode_cd_radius, mode_icon_y + mode_cd_radius, mode_cd_radius, COLOR2D(192, 192, 192));
    
    // Draw inner hole of mode CD icon
    m_Graphics.DrawCircle(mode_icon_x + mode_cd_radius, mode_icon_y + mode_cd_radius, 2, COLOR2D(255, 255, 255));
    
    // Draw button bar at bottom
    m_Graphics.DrawRect(0, 190, WIDTH, 50, COLOR2D(58, 124, 165));
    
    // Draw button labels
    // A button
    m_Graphics.DrawText(12, 200, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignLeft);
    
    // Draw up arrow for A button
    unsigned arrow_x = 30;
    unsigned arrow_y = 205;
    m_Graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y - 8, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(arrow_x - 4, arrow_y - 4, arrow_x, arrow_y - 8, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(arrow_x + 4, arrow_y - 4, arrow_x, arrow_y - 8, COLOR2D(0, 0, 0));
    
    // B button
    m_Graphics.DrawText(72, 200, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignLeft);
    
    // Draw down arrow for B button
    arrow_x = 90;
    arrow_y = 205;
    m_Graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 8, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(arrow_x - 4, arrow_y + 4, arrow_x, arrow_y + 8, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(arrow_x + 4, arrow_y + 4, arrow_x, arrow_y + 8, COLOR2D(0, 0, 0));
    
    // X button
    m_Graphics.DrawText(132, 200, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignLeft);
    
    // Draw hamburger menu icon for X button
    unsigned menu_x = 150;
    unsigned menu_y = 200;
    m_Graphics.DrawLine(menu_x, menu_y + 1, menu_x + 20, menu_y + 1, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(menu_x, menu_y + 8, menu_x + 20, menu_y + 8, COLOR2D(0, 0, 0));
    m_Graphics.DrawLine(menu_x, menu_y + 15, menu_x + 20, menu_y + 15, COLOR2D(0, 0, 0));
    
    // Y button
    m_Graphics.DrawText(192, 200, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignLeft);
    
    // Draw folder icon for Y button
    unsigned folder_x = 210;
    unsigned folder_y = 198;
    
    // Folder outline
    m_Graphics.DrawRectOutline(folder_x, folder_y + 5, 20, 15, COLOR2D(0, 0, 0));
    m_Graphics.DrawRectOutline(folder_x + 2, folder_y, 8, 5, COLOR2D(0, 0, 0));
    
    // Folder fill
    m_Graphics.DrawRect(folder_x + 1, folder_y + 6, 18, 13, COLOR2D(255, 223, 128));
    m_Graphics.DrawRect(folder_x + 3, folder_y + 1, 6, 3, COLOR2D(255, 223, 128));
    
    // Update the display with all the graphics we've drawn
    m_Graphics.UpdateDisplay();
    
    // Create a loop to keep the display active and check for button presses
    for (int i = 0; i < 60; i++)  // Run for 60 seconds
    {
        // Every 3 seconds, explicitly turn the display on again to prevent sleep mode
        if (i % 3 == 0)
        {
            m_Display.On();
            m_Graphics.UpdateDisplay();
        }
        
        // Check Button A (Up)
        boolean bCurrentAState = m_ButtonA.Read();
        if (bCurrentAState == LOW && m_bLastButtonAState == HIGH)
        {
            // Button A was pressed
            DisplayButtonPress("A (Up)");
        }
        m_bLastButtonAState = bCurrentAState;
        
        // Check Button B (Down)
        boolean bCurrentBState = m_ButtonB.Read();
        if (bCurrentBState == LOW && m_bLastButtonBState == HIGH)
        {
            // Button B was pressed
            DisplayButtonPress("B (Down)");
        }
        m_bLastButtonBState = bCurrentBState;
        
        // Check Button X (Cancel)
        boolean bCurrentXState = m_ButtonX.Read();
        if (bCurrentXState == LOW && m_bLastButtonXState == HIGH)
        {
            // Button X was pressed
            DisplayButtonPress("X (Cancel)");
        }
        m_bLastButtonXState = bCurrentXState;
        
        // Check Button Y (Select)
        boolean bCurrentYState = m_ButtonY.Read();
        if (bCurrentYState == LOW && m_bLastButtonYState == HIGH)
        {
            // Button Y was pressed
            DisplayButtonPress("Y (Select)");
        }
        m_bLastButtonYState = bCurrentYState;
        
        // Short delay to prevent CPU hogging
        m_Timer.MsDelay(100);
    }
    
    m_Display.Off();
    
    return ShutdownReboot;
}
