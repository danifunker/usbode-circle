//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2018  R. Stange <rsta2@o2online.de>
// 
// Modified for SH1106 SPI display
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
#include <circle/time.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/machineinfo.h>
#include <circle/2dgraphics.h>
#include <usbode-display/sh1106display.h>  // Add this include

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
    m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    // Initialize SPI Master with clock speed and mode from CSH1106Display class
    m_SPIMaster (CSH1106Display::SPI_CLOCK_SPEED, 
                 CSH1106Display::SPI_CPOL, 
                 CSH1106Display::SPI_CPHA),
    // Initialize SH1106 display with SPI
    m_Display (&m_SPIMaster, 
              CSH1106Display::DC_PIN, 
              CSH1106Display::RESET_PIN, 
              CSH1106Display::OLED_WIDTH, 
              CSH1106Display::OLED_HEIGHT, 
              CSH1106Display::SPI_CLOCK_SPEED, 
              CSH1106Display::SPI_CPOL, 
              CSH1106Display::SPI_CPHA, 
              CSH1106Display::SPI_CHIP_SELECT),
    // Initialize SH1106 device with the display, using Font8x8
    m_LCD (&m_SPIMaster, &m_Display, 
          CSH1106Display::DISPLAY_COLUMNS, 
          CSH1106Display::DISPLAY_ROWS, 
          Font8x8, FALSE, FALSE)
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
    
    // Initialize SPI
    if (bOK)
    {
        bOK = m_SPIMaster.Initialize ();
    }

    // Initialize display first
    if (bOK)
    {
        bOK = m_Display.Initialize ();
    }

    // Then initialize device
    if (bOK)
    {
        bOK = m_LCD.Initialize ();
    }
    
    return bOK;
}

TShutdownMode CKernel::Run (void)
{
    m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
    //m_Logger.Write (FromKernel, LogNotice, "SH1106 SPI Display Test");

    // Start with the button demo (prioritize this for testing)
    //ButtonDemo();
    //CTimer::Get()->MsDelay(2000);
    
    // Continue with other demos if the button test completes
    //m_Logger.Write (FromKernel, LogNotice, "Starting Font Demo");
    LCDWrite ("\E[H\E[J"); // Reset cursor and clear display
    LCDWrite ("Font8x8 Demo");
    LCDWrite ("\n21x8 chars");
    
    CTimer::Get()->MsDelay(2000);
    
    // Show our custom display example
    unsigned startTime = m_Timer.GetTicks();
    
    // Debug the start time
    m_Logger.Write(FromKernel, LogNotice, "Starting timer at: %u ms", startTime);
    
    if (startTime == 0) {
        m_Logger.Write(FromKernel, LogWarning, "Timer not initialized correctly!");
        // Wait for timer to initialize
        CTimer::Get()->MsDelay(100);
        startTime = m_Timer.GetTicks();
    }
    
    CustomDisplayWithTimer(startTime);
    //ButtonDemo();
    CTimer::Get()->MsDelay(5000);
    
    // Run the time demo that displays the current time

    return ShutdownHalt;
}

void CKernel::LCDWrite (const char *pString)
{
    m_LCD.Write (pString, strlen (pString));
}

// New method that includes a timer instead of "1 (CD)" and button press detection
void CKernel::CustomDisplayWithTimer(unsigned startTime)
{
    // Create a 2D graphics object that uses our SH1106 display
    C2DGraphics graphics(&m_Display);
    
    // Use the class constants but get labels from the device class
    const unsigned NUM_PINS = CSH1106Display::NUM_PINS;
    const unsigned* pins = CSH1106Display::BUTTON_PINS;
    const char** pinLabels = CSH1106Device::GPIO_BUTTON_LABELS;  // Changed from CSH1106Display::BUTTON_LABELS
    
    // Add animation to show program is running
    char animation[4] = {'|', '/', '-', '\\'};
    unsigned animCounter = 0;
    unsigned lastAnimUpdate = 0;
    
    // Check that startTime is non-zero (fallback if it is)
    if (startTime == 0) {
        m_Logger.Write(FromKernel, LogWarning, "startTime was 0, resetting");
        startTime = m_Timer.GetTicks();
    }
    
    // Log initial timer value for debugging
    m_Logger.Write(FromKernel, LogNotice, "CustomDisplayWithTimer starting at time: %u", startTime);
    
    // Initialize GPIO pins with pull-ups (following ButtonDemo pattern)
    CGPIOPin* gpioPins[NUM_PINS];
    for (unsigned i = 0; i < NUM_PINS; i++) {
        gpioPins[i] = new CGPIOPin(pins[i], GPIOModeInputPullUp);
    }
    
    // Give pins time to stabilize
    CTimer::Get()->MsDelay(100);
    
    // Draw the initial display
    m_Display.Clear(SH1106_BLACK_COLOR);
    
    // Version text at the top
    static const char VersionText[] = "USBODE v:1.99";
    m_Display.DrawText(0, 2, VersionText, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                      FALSE, FALSE, Font8x8);

    // Initial animation character
    char animChar[2] = {'|', 0};
    m_Display.DrawText(120, 2, animChar, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                      FALSE, FALSE, Font8x8);

    // Draw WiFi icon - using 2D graphics methods with white color
    int wifi_x = 0;
    int wifi_y = 16;
    
    // Outer arc (approximated with points/lines)
    graphics.DrawLine(wifi_x+2, wifi_y, wifi_x, wifi_y+2, COLOR2D(255, 255, 255));
    graphics.DrawLine(wifi_x, wifi_y+2, wifi_x, wifi_y+3, COLOR2D(255, 255, 255));
    graphics.DrawLine(wifi_x, wifi_y+3, wifi_x+2, wifi_y+5, COLOR2D(255, 255, 255));
    
    // Inner arc
    graphics.DrawLine(wifi_x+3, wifi_y+2, wifi_x+2, wifi_y+3, COLOR2D(255, 255, 255));
    graphics.DrawLine(wifi_x+2, wifi_y+3, wifi_x+3, wifi_y+4, COLOR2D(255, 255, 255));
    
    // Center dot
    graphics.DrawPixel(wifi_x+4, wifi_y+4, COLOR2D(255, 255, 255));

    // IP address (shifted down by 4px)
    static const char IPAddress[] = "192.168.1.100";
    m_Display.DrawText(10, 14, IPAddress, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                      FALSE, FALSE, Font8x8);

    // Draw CD icon - using circle drawing functions
    int cd_x = 0;
    int cd_y = 27;
    int cd_radius = 5;
    
    // Outer circle
    graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(255, 255, 255));
    
    // Inner circle (hole)
    graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 1, COLOR2D(255, 255, 255));

    // ISO name (with two-line support)
    static const char ISOName[] = "Windows10_64bit_Pro.iso"; // Shortened to fit better
    size_t first_line_chars = 16;
    size_t second_line_chars = 16;
    
    char first_line[32] = {0};
    char second_line[32] = {0};
    size_t iso_length = strlen(ISOName);
    
    if (iso_length <= first_line_chars) {
        // Short name fits on one line
        m_Display.DrawText(12, 27, ISOName, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                          FALSE, FALSE, Font8x8);
    } else {
        // First line (with CD icon offset)
        strncpy(first_line, ISOName, first_line_chars);
        first_line[first_line_chars] = '\0';
        m_Display.DrawText(12, 27, first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                          FALSE, FALSE, Font8x8);
        
        // Second line (potentially with ellipsis for very long names)
        if (iso_length > first_line_chars + second_line_chars - 4) {
            // Very long name, use ellipsis and last 9 chars
            strncpy(second_line, ISOName + first_line_chars, second_line_chars - 13);
            strcat(second_line, "...");
            strcat(second_line, ISOName + iso_length - 9);
        } else {
            strncpy(second_line, ISOName + first_line_chars, second_line_chars);
            second_line[second_line_chars] = '\0';
        }
        
        m_Display.DrawText(0, 37, second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                          FALSE, FALSE, Font8x8);
    }
    
    // Draw USB icon - using line drawing from 2D graphics
    int usb_x = 0;
    int usb_y = 49;
    
    // Horizontal line
    graphics.DrawLine(usb_x, usb_y + 4, usb_x + 10, usb_y + 4, COLOR2D(255, 255, 255));
    
    // Circle at left (approximated with a small circle)
    graphics.DrawCircleOutline(usb_x - 1, usb_y + 4, 2, COLOR2D(255, 255, 255));
    
    // Top arm
    graphics.DrawLine(usb_x + 2, usb_y + 4, usb_x + 2, usb_y, COLOR2D(255, 255, 255));
    graphics.DrawLine(usb_x + 2, usb_y, usb_x + 6, usb_y, COLOR2D(255, 255, 255));
    
    // Bottom arm
    graphics.DrawLine(usb_x + 6, usb_y + 4, usb_x + 6, usb_y + 8, COLOR2D(255, 255, 255));
    graphics.DrawLine(usb_x + 6, usb_y + 8, usb_x + 10, usb_y + 8, COLOR2D(255, 255, 255));
        
    // Update display
    m_Display.Refresh();
    
    // Initial state - similar to ButtonDemo
    bool buttonPressed = false;
    unsigned buttonDisplayTime = 0;
    
    // Run the main detection loop (with 30 second timeout)
    unsigned long timeout = m_Timer.GetTicks() + 30 * 1000;
    
    // Main loop - following ButtonDemo pattern
    while (m_Timer.GetTicks() < timeout) {
        // Current tick count
        unsigned nCurrentTime = m_Timer.GetTicks();
        
        // Animation update - ALWAYS update every 250ms regardless of other states
        if (nCurrentTime - lastAnimUpdate >= 250) {
            // Update animation character
            animCounter++;
            char animChar[2] = {animation[animCounter % 4], 0};
            m_Display.DrawText(120, 2, animChar, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                              FALSE, FALSE, Font8x8);
            
            m_Display.Refresh();
            
            lastAnimUpdate = nCurrentTime;
        }
        
        // Occasionally log timer info
        if (nCurrentTime % 5000 < 10) {
            m_Logger.Write(FromKernel, LogDebug, "Timer debug: current=%u, start=%u, diff=%u", 
                          nCurrentTime, startTime, nCurrentTime - startTime);
        }
        
        // Check all pins for button presses
        if (!buttonPressed) {
            for (unsigned i = 0; i < NUM_PINS; i++) {
                // Read current pin state (active LOW with pullup)
                bool isActive = gpioPins[i]->Read() == LOW;
                
                if (isActive) {
                    // New button press detected
                    m_Logger.Write(FromKernel, LogNotice, "GPIO %u (%s) button PRESSED", 
                                 pins[i], pinLabels[i]);
                    
                    // 1. Clear the entire screen for clean button display
                    m_Display.Clear(SH1106_BLACK_COLOR);
                    
                    // 2. Show only the button name (simplified display)
                    m_Display.DrawText(30, 28, "Button:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                      FALSE, FALSE, Font8x8);
                    
                    // 3. Draw button name with highlight - centered
                    m_Display.DrawText(40, 38, pinLabels[i], SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                      TRUE, FALSE, Font8x8);
                    
                    // Update display
                    m_Display.Refresh();
                    
                    // Record button state and time - only show for 0.5 seconds
                    buttonPressed = true;
                    buttonDisplayTime = nCurrentTime;
                    
                    // Short debounce delay
                    CTimer::Get()->MsDelay(20);
                    
                    break;
                }
            }
        }
        
        // Check if we need to return to the main display after button press
        // Reduced to 500ms (0.5 seconds)
        if (buttonPressed && (nCurrentTime - buttonDisplayTime >= 500)) {
            // Completely redraw the original screen to avoid artifacts
            
            // 1. Clear the entire screen
            m_Display.Clear(SH1106_BLACK_COLOR);
            
            // 2. Redraw version text
            m_Display.DrawText(0, 2, VersionText, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                              FALSE, FALSE, Font8x8);
            
            // 3. Redraw animation character
            animCounter++;
            char animChar[2] = {animation[animCounter % 4], 0};
            m_Display.DrawText(120, 2, animChar, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                              FALSE, FALSE, Font8x8);
            
            // 4. Redraw WiFi icon
            graphics.DrawLine(wifi_x+2, wifi_y, wifi_x, wifi_y+2, COLOR2D(255, 255, 255));
            graphics.DrawLine(wifi_x, wifi_y+2, wifi_x, wifi_y+3, COLOR2D(255, 255, 255));
            graphics.DrawLine(wifi_x, wifi_y+3, wifi_x+2, wifi_y+5, COLOR2D(255, 255, 255));
            graphics.DrawLine(wifi_x+3, wifi_y+2, wifi_x+2, wifi_y+3, COLOR2D(255, 255, 255));
            graphics.DrawLine(wifi_x+2, wifi_y+3, wifi_x+3, wifi_y+4, COLOR2D(255, 255, 255));
            graphics.DrawPixel(wifi_x+4, wifi_y+4, COLOR2D(255, 255, 255));
            
            // 4. Redraw IP address
            m_Display.DrawText(10, 14, IPAddress, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                              FALSE, FALSE, Font8x8);
            
            // 5. Redraw CD icon
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(255, 255, 255));
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 1, COLOR2D(255, 255, 255));
            
            // 6. Redraw ISO name
            if (iso_length <= first_line_chars) {
                m_Display.DrawText(12, 27, ISOName, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                  FALSE, FALSE, Font8x8);
            } else {
                m_Display.DrawText(12, 27, first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                  FALSE, FALSE, Font8x8);
                m_Display.DrawText(0, 37, second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                  FALSE, FALSE, Font8x8);
            }
            
            // 7. Redraw USB icon
            graphics.DrawLine(usb_x, usb_y + 4, usb_x + 10, usb_y + 4, COLOR2D(255, 255, 255));
            graphics.DrawCircleOutline(usb_x - 1, usb_y + 4, 2, COLOR2D(255, 255, 255));
            graphics.DrawLine(usb_x + 2, usb_y + 4, usb_x + 2, usb_y, COLOR2D(255, 255, 255));
            graphics.DrawLine(usb_x + 2, usb_y, usb_x + 6, usb_y, COLOR2D(255, 255, 255));
            graphics.DrawLine(usb_x + 6, usb_y + 4, usb_x + 6, usb_y + 8, COLOR2D(255, 255, 255));
            graphics.DrawLine(usb_x + 6, usb_y + 8, usb_x + 10, usb_y + 8, COLOR2D(255, 255, 255));
            
            // 8. Single refresh of the display
            m_Display.Refresh();
            
            // Reset button state
            buttonPressed = false;
            lastAnimUpdate = nCurrentTime;
        }
    }
    
    // Clean up pin objects
    for (unsigned i = 0; i < NUM_PINS; i++) {
        delete gpioPins[i];
    }
}