//
// displaymanager.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2024  Your Name
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
#include "displaymanager.h"
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/timer.h>
#include <circle/2dgraphics.h>
#include "kernel.h"

#include <assert.h>

static const char FromDisplayManager[] = "dispman";

// Updated constructor that takes display type directly
CDisplayManager::CDisplayManager(CLogger *pLogger, TDisplayType DisplayType)
    : m_pLogger(pLogger),
      m_DisplayType(DisplayType),
      m_pSH1106Display(nullptr),
      m_pSH1106Device(nullptr)
      // Initialize ST7789 pointers when implemented
      // m_pST7789Display(nullptr),
      // m_pST7789Device(nullptr)
{
    assert(m_pLogger != nullptr);
}

CDisplayManager::~CDisplayManager(void)
{
    // Clean up SH1106 components
    if (m_pSH1106Device != nullptr)
    {
        delete m_pSH1106Device;
        m_pSH1106Device = nullptr;
    }
    
    if (m_pSH1106Display != nullptr)
    {
        delete m_pSH1106Display;
        m_pSH1106Display = nullptr;
    }
    
    // Clean up ST7789 components when implemented
    /*
    if (m_pST7789Device != nullptr)
    {
        delete m_pST7789Device;
        m_pST7789Device = nullptr;
    }
    
    if (m_pST7789Display != nullptr)
    {
        delete m_pST7789Display;
        m_pST7789Display = nullptr;
    }
    */
}

boolean CDisplayManager::Initialize(CSPIMaster *pSPIMaster)
{
    assert(pSPIMaster != nullptr);
    
    // Log the display type
    const char *pDisplayTypeStr = "Unknown";
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        pDisplayTypeStr = "SH1106";
        break;
    case DisplayTypeST7789:
        pDisplayTypeStr = "ST7789";
        break;
    default:
        pDisplayTypeStr = "Unknown";
        break;
    }
    
    m_pLogger->Write(FromDisplayManager, LogNotice, "Initializing %s display", pDisplayTypeStr);
    
    // Initialize the appropriate display
    boolean bResult = FALSE;
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        bResult = InitializeSH1106(pSPIMaster);
        break;
        
    case DisplayTypeST7789:
        bResult = InitializeST7789(pSPIMaster);
        break;
        
    default:
        m_pLogger->Write(FromDisplayManager, LogError, "Unknown display type");
        bResult = FALSE;
        break;
    }
    
    if (!bResult)
    {
        m_pLogger->Write(FromDisplayManager, LogError, "Display initialization failed");
    }
    
    return bResult;
}

boolean CDisplayManager::InitializeSH1106(CSPIMaster *pSPIMaster)
{
    // Create SH1106 display
    m_pSH1106Display = new CSH1106Display(
        pSPIMaster,
        CSH1106Display::DC_PIN,
        CSH1106Display::RESET_PIN,
        CSH1106Display::OLED_WIDTH,
        CSH1106Display::OLED_HEIGHT,
        CSH1106Display::SPI_CLOCK_SPEED,
        CSH1106Display::SPI_CPOL,
        CSH1106Display::SPI_CPHA,
        CSH1106Display::SPI_CHIP_SELECT);
    
    if (m_pSH1106Display == nullptr)
    {
        m_pLogger->Write(FromDisplayManager, LogError, "Failed to create SH1106 display");
        return FALSE;
    }
    
    // Initialize the display
    if (!m_pSH1106Display->Initialize())
    {
        m_pLogger->Write(FromDisplayManager, LogError, "Failed to initialize SH1106 display");
        delete m_pSH1106Display;
        m_pSH1106Display = nullptr;
        return FALSE;
    }
    
    // Create SH1106 device
    m_pSH1106Device = new CSH1106Device(
        pSPIMaster,
        m_pSH1106Display,
        CSH1106Display::DISPLAY_COLUMNS,
        CSH1106Display::DISPLAY_ROWS,
        Font6x7,  // Using smallerer font by default
        FALSE,    // Not double width
        FALSE);   // Not double height
    
    if (m_pSH1106Device == nullptr)
    {
        m_pLogger->Write(FromDisplayManager, LogError, "Failed to create SH1106 device");
        delete m_pSH1106Display;
        m_pSH1106Display = nullptr;
        return FALSE;
    }
    
    // Initialize the device
    if (!m_pSH1106Device->Initialize())
    {
        m_pLogger->Write(FromDisplayManager, LogError, "Failed to initialize SH1106 device");
        delete m_pSH1106Device;
        m_pSH1106Device = nullptr;
        delete m_pSH1106Display;
        m_pSH1106Display = nullptr;
        return FALSE;
    }
    
    // Display initialized successfully
    m_pLogger->Write(FromDisplayManager, LogNotice, "SH1106 display initialized successfully");
    
    return TRUE;
}

boolean CDisplayManager::InitializeST7789(CSPIMaster *pSPIMaster)
{
    // Create ST7789 display with correct parameters based on the sample code
    m_pST7789Display = new CST7789Display(
        pSPIMaster,
        9,                  // DC_PIN from sample
        27,                 // RESET_PIN from sample
        CST7789Display::None,  // BACKLIGHT_PIN
        240,                // WIDTH from sample
        240,                // HEIGHT from sample
        0,                  // SPI_CPOL from sample
        0,                  // SPI_CPHA from sample
        80000000,           // SPI_CLOCK_SPEED from sample (80MHz) - MUST match SPIMaster
        1);                 // SPI_CHIP_SELECT from sample
    
    if (m_pST7789Display == nullptr)
    {
        m_pLogger->Write("dispman", LogError, "Failed to create ST7789 display");
        return FALSE;
    }
    
    // Initialize the display
    if (!m_pST7789Display->Initialize())
    {
        m_pLogger->Write("dispman", LogError, "Failed to initialize ST7789 display");
        delete m_pST7789Display;
        m_pST7789Display = nullptr;
        return FALSE;
    }
    
    // Set rotation to 270 degrees for the Pirate Audio display
    m_pST7789Display->SetRotation(270);
    
    // Create a 2D graphics instance for drawing
    C2DGraphics graphics(m_pST7789Display);
    if (!graphics.Initialize())
    {
        m_pLogger->Write("dispman", LogError, "Failed to initialize 2D graphics");
        delete m_pST7789Display;
        m_pST7789Display = nullptr;
        return FALSE;
    }
    
    // Initialize with WHITE background
    graphics.ClearScreen(COLOR2D(255, 255, 255));
    
    // Update display explicitly
    graphics.UpdateDisplay();
    
    // Turn the display on to prevent sleep mode
    m_pST7789Display->On();
    
    // Display initialized successfully
    m_pLogger->Write("dispman", LogNotice, "ST7789 display initialized successfully");
    
    return TRUE;
}

CDevice *CDisplayManager::GetDisplayDevice(void) const
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        return m_pSH1106Device;
        
    case DisplayTypeST7789:
        // return m_pST7789Device;
        return nullptr;  // Not implemented yet
        
    default:
        return nullptr;
    }
}

void CDisplayManager::ClearDisplay(void)
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
        }
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            m_pST7789Display->Clear(ST7789_WHITE_COLOR);
        }        break;
        
    default:
        break;
    }
}

// Update ShowStatusScreen to support ST7789 displays

void CDisplayManager::ShowStatusScreen(const char *pTitle, const char *pIPAddress, const char *pISOName, const char *pUSBSpeed)
{
    assert(pTitle != nullptr);
    assert(pIPAddress != nullptr);
    assert(pISOName != nullptr);
    assert(pUSBSpeed != nullptr);
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw title at the top
            m_pSH1106Display->DrawText(0, 2, pTitle, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font8x8);
            
            // Draw WiFi icon using pixel operations
            unsigned int wifi_x = 0;
            unsigned int wifi_y = 14;
            
            // WiFi base dot (center)
            m_pSH1106Display->SetPixel(wifi_x+4, wifi_y+6, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            m_pSH1106Display->SetPixel(wifi_x+4, wifi_y+5, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            
            // Inner arc
            for (unsigned int x = wifi_x+2; x <= wifi_x+6; x++) {
                m_pSH1106Display->SetPixel(x, wifi_y+4, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                m_pSH1106Display->SetPixel(x, wifi_y+3, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // Middle arc
            for (unsigned int x = wifi_x+1; x <= wifi_x+7; x++) {
                m_pSH1106Display->SetPixel(x, wifi_y+2, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            for (unsigned int x = wifi_x; x <= wifi_x+8; x++) {
                m_pSH1106Display->SetPixel(x, wifi_y+1, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // Outer arc
            for (unsigned int x = wifi_x; x <= wifi_x+8; x++) {
                m_pSH1106Display->SetPixel(x, wifi_y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
                                     
            // Draw IP address
            m_pSH1106Display->DrawText(10, 14, pIPAddress, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Draw CD icon
            unsigned int cd_x = 0;
            unsigned int cd_y = 27;
            
            // Draw CD as a ring
            for (int y = -4; y <= 4; y++) {
                for (int x = -4; x <= 4; x++) {
                    int dist_squared = x*x + y*y;
                    // Draw pixels between inner and outer radius
                    if (dist_squared <= 16 && dist_squared > 4) {
                        unsigned int px = cd_x+4+x;
                        unsigned int py = cd_y+4+y;
                        // Ensure coordinates are in valid range
                        if (px < CSH1106Display::OLED_WIDTH && py < CSH1106Display::OLED_HEIGHT) {
                            m_pSH1106Display->SetPixel(px, py, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                        }
                    }
                }
            }
            
            // ISO name (with two-line support)
            size_t first_line_chars = 19;  // Increased from 16 to 19
            size_t second_line_chars = 21; // Increased from 18 to 21
            
            char first_line[32] = {0};
            char second_line[32] = {0};
            size_t iso_length = strlen(pISOName);
            
            if (iso_length <= first_line_chars)
            {
                // Short name fits on one line
                m_pSH1106Display->DrawText(12, 27, pISOName, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                         FALSE, FALSE, Font6x7);
            }
            else
            {
                // First line (with CD icon offset)
                strncpy(first_line, pISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                m_pSH1106Display->DrawText(12, 27, first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                         FALSE, FALSE, Font6x7);
                
                // Second line (potentially with ellipsis for very long names)
                if (iso_length > first_line_chars + second_line_chars - 4)
                {
                    // Very long name, use ellipsis and last 13 chars (increased from 9)
                    strncpy(second_line, pISOName + first_line_chars, second_line_chars - 17); // Adjust for "..." + 13 chars
                    strcat(second_line, "...");
                    strcat(second_line, pISOName + iso_length - 13); // Show last 13 chars instead of 9
                }
                else
                {
                    strncpy(second_line, pISOName + first_line_chars, second_line_chars);
                    second_line[second_line_chars] = '\0';
                }
                
                m_pSH1106Display->DrawText(0, 37, second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                         FALSE, FALSE, Font6x7);
            }
            
            // Draw USB icon - pixel by pixel for better control
            unsigned int usb_x = 0;
            unsigned int usb_y = 49;
            
            // USB outline - rectangular shape
            for (unsigned int x = usb_x; x <= usb_x+8; x++) {
                // Top and bottom lines
                m_pSH1106Display->SetPixel(x, usb_y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                m_pSH1106Display->SetPixel(x, usb_y+7, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            for (unsigned int y = usb_y; y <= usb_y+7; y++) {
                // Left and right sides
                m_pSH1106Display->SetPixel(usb_x, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                m_pSH1106Display->SetPixel(usb_x+8, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // USB pins
            for (unsigned int y = usb_y+2; y <= usb_y+5; y++) {
                // Left pin
                m_pSH1106Display->SetPixel(usb_x+2, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                m_pSH1106Display->SetPixel(usb_x+3, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                
                // Right pin
                m_pSH1106Display->SetPixel(usb_x+5, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                m_pSH1106Display->SetPixel(usb_x+6, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // Draw USB speed info next to the USB icon
            m_pSH1106Display->DrawText(10, 49, pUSBSpeed, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Ensure the display is updated with all changes
            m_pSH1106Display->Refresh();
        }
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            // Create a 2D graphics instance for drawing
            C2DGraphics graphics(m_pST7789Display);
            if (!graphics.Initialize())
            {
                m_pLogger->Write("dispman", LogError, "Failed to initialize 2D graphics");
                return;
            }
            
            // Clear the screen with WHITE background using the graphics object
            graphics.ClearScreen(COLOR2D(255, 255, 255));
            
            // Draw header bar with blue background
            graphics.DrawRect(0, 0, m_pST7789Display->GetWidth(), 30, COLOR2D(58, 124, 165));
            
            // Draw title text in white - INCREASED FONT SIZE
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);
            
            // Draw WiFi icon as an antenna (more like waveshare) - REDESIGNED
            unsigned wifi_x = 10;
            unsigned wifi_y = 40;
            
            // Draw WiFi antenna as vertical line with radiating arcs
            // Vertical line (antenna)
            graphics.DrawLine(wifi_x + 10, wifi_y, wifi_x + 10, wifi_y + 20, COLOR2D(0, 0, 0));
            
            // Base of antenna
            graphics.DrawLine(wifi_x + 5, wifi_y + 20, wifi_x + 15, wifi_y + 20, COLOR2D(0, 0, 0));
            
            // Since DrawArc is not available, simulate arcs using small lines
            // Right side small arc
            graphics.DrawLine(wifi_x + 10, wifi_y + 15, wifi_x + 13, wifi_y + 12, COLOR2D(0, 0, 0));
            graphics.DrawLine(wifi_x + 13, wifi_y + 12, wifi_x + 15, wifi_y + 12, COLOR2D(0, 0, 0));
            
            // Left side small arc
            graphics.DrawLine(wifi_x + 10, wifi_y + 15, wifi_x + 7, wifi_y + 12, COLOR2D(0, 0, 0));
            graphics.DrawLine(wifi_x + 7, wifi_y + 12, wifi_x + 5, wifi_y + 12, COLOR2D(0, 0, 0));
            
            // Right side large arc
            graphics.DrawLine(wifi_x + 10, wifi_y + 15, wifi_x + 15, wifi_y + 10, COLOR2D(0, 0, 0));
            graphics.DrawLine(wifi_x + 15, wifi_y + 10, wifi_x + 18, wifi_y + 10, COLOR2D(0, 0, 0));
            
            // Left side large arc
            graphics.DrawLine(wifi_x + 10, wifi_y + 15, wifi_x + 5, wifi_y + 10, COLOR2D(0, 0, 0));
            graphics.DrawLine(wifi_x + 5, wifi_y + 10, wifi_x + 2, wifi_y + 10, COLOR2D(0, 0, 0));
            
            // Draw IP address text (BLACK) - INCREASED FONT SIZE by adjusting position
            graphics.DrawText(35, 45, COLOR2D(0, 0, 0), pIPAddress, C2DGraphics::AlignLeft);
            
            // Draw CD icon - IMPROVED with clearer center hole
            unsigned cd_x = 10;
            unsigned cd_y = 70;
            unsigned cd_radius = 10;
            
            // Draw outer circle of CD
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(0, 0, 0));
            
            // Draw middle circle of CD (making it more visible)
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(0, 0, 0));
            
            // Draw center hole of CD
            graphics.DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(0, 0, 0));
            
            // ISO name handling with two-line support
            // FIXED ISSUE WITH SECOND LINE NOT SHOWING
            size_t first_line_chars = 25;  // Reduced from 30 for larger font
            size_t second_line_chars = 30; // Reduced from 35 for larger font
            
            char first_line[40] = {0};
            char second_line[40] = {0};
            size_t iso_length = strlen(pISOName);
            
            if (iso_length <= first_line_chars)
            {
                // Short name fits on one line - INCREASED FONT SIZE with y position
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), pISOName, C2DGraphics::AlignLeft);
            }
            else
            {
                // First line - INCREASED FONT SIZE
                strncpy(first_line, pISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                
                // Second line (potentially with ellipsis for very long names)
                if (iso_length > first_line_chars + second_line_chars - 4)
                {
                    // Very long name, use ellipsis and last portion
                    strncpy(second_line, pISOName + first_line_chars, second_line_chars - 17);
                    strcat(second_line, "...");
                    strcat(second_line, pISOName + iso_length - 13);
                }
                else
                {
                    strncpy(second_line, pISOName + first_line_chars, second_line_chars);
                    second_line[second_line_chars] = '\0';
                }
                
                // INCREASED FONT SIZE and explicitly ensure second_line is null-terminated
                second_line[sizeof(second_line) - 1] = '\0';
                graphics.DrawText(35, 95, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
            }
            
            // Draw USB icon - MOVED DOWN to bottom of screen before button bar
            unsigned usb_x = 10;
            unsigned usb_y = 160; // Moved down from 120
            
            // Draw USB icon - horizontal line (main stem)
            graphics.DrawLine(usb_x, usb_y + 8, usb_x + 20, usb_y + 8, COLOR2D(0, 0, 0));
            
            // Draw circle at left of USB icon
            graphics.DrawCircleOutline(usb_x - 2, usb_y + 8, 4, COLOR2D(0, 0, 0));
            
            // Draw USB icon - top arm
            graphics.DrawLine(usb_x + 6, usb_y + 8, usb_x + 6, usb_y, COLOR2D(0, 0, 0));
            graphics.DrawLine(usb_x + 6, usb_y, usb_x + 14, usb_y, COLOR2D(0, 0, 0));
            
            // Draw USB icon - bottom arm
            graphics.DrawLine(usb_x + 14, usb_y + 8, usb_x + 14, usb_y + 16, COLOR2D(0, 0, 0));
            graphics.DrawLine(usb_x + 14, usb_y + 16, usb_x + 22, usb_y + 16, COLOR2D(0, 0, 0));
            
            // Draw USB mode text - INCREASED FONT SIZE and moved down
            graphics.DrawText(40, 165, COLOR2D(0, 0, 0), pUSBSpeed, C2DGraphics::AlignLeft);
            
            // Draw button bar at bottom
            graphics.DrawRect(0, 190, m_pST7789Display->GetWidth(), 50, COLOR2D(58, 124, 165));
            
            // UPDATED NAVIGATION BUTTONS with correct icons
            // A button with UP arrow
            graphics.DrawText(12, 205, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignLeft);
            
            // Up arrow
            unsigned arrow_x = 30;
            unsigned arrow_y = 205;
            graphics.DrawLine(arrow_x, arrow_y - 8, arrow_x, arrow_y, COLOR2D(255, 255, 255));
            graphics.DrawLine(arrow_x - 4, arrow_y - 4, arrow_x, arrow_y - 8, COLOR2D(255, 255, 255));
            graphics.DrawLine(arrow_x + 4, arrow_y - 4, arrow_x, arrow_y - 8, COLOR2D(255, 255, 255));
            
            // B button with DOWN arrow
            graphics.DrawText(72, 205, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignLeft);
            
            // Down arrow
            arrow_x = 90;
            arrow_y = 205;
            graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 8, COLOR2D(255, 255, 255));
            graphics.DrawLine(arrow_x - 4, arrow_y + 4, arrow_x, arrow_y + 8, COLOR2D(255, 255, 255));
            graphics.DrawLine(arrow_x + 4, arrow_y + 4, arrow_x, arrow_y + 8, COLOR2D(255, 255, 255));
            
            // X button with MENU BARS (3 horizontal lines)
            graphics.DrawText(132, 205, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignLeft);
            
            // Draw 3 horizontal bars for menu
            unsigned menu_x = 150;
            unsigned menu_y = 200;
            graphics.DrawLine(menu_x, menu_y, menu_x + 15, menu_y, COLOR2D(255, 255, 255));
            graphics.DrawLine(menu_x, menu_y + 5, menu_x + 15, menu_y + 5, COLOR2D(255, 255, 255));
            graphics.DrawLine(menu_x, menu_y + 10, menu_x + 15, menu_y + 10, COLOR2D(255, 255, 255));
            
            // Y button with FOLDER icon
            graphics.DrawText(192, 205, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignLeft);
            
            // Draw folder icon
            unsigned folder_x = 210;
            unsigned folder_y = 200;
            
            // Folder base
            graphics.DrawRect(folder_x, folder_y + 3, 16, 10, COLOR2D(255, 255, 255));
            // Folder tab
            graphics.DrawRect(folder_x + 2, folder_y, 8, 3, COLOR2D(255, 255, 255));
            
            // Update the display with all the graphics we've drawn
            graphics.UpdateDisplay();
            
            // Ensure display stays on
            m_pST7789Display->On();
        }
        break;
    
    default:
        break;
    }
}

CDisplay *CDisplayManager::GetDisplay(void) const
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        return m_pSH1106Display;
        
    case DisplayTypeST7789:
        return m_pST7789Display;
        
    default:
        return nullptr;
    }
}

void CDisplayManager::Refresh(void)
{
    // Call the appropriate display's refresh method based on type
    if (m_pSH1106Display != nullptr)
    {
        m_pSH1106Display->Refresh();
    }
    else if (m_pST7789Display != nullptr)
    {
        // For now, log that we tried to refresh ST7789 but it's not implemented
        if (m_pLogger != nullptr)
        {
            m_pLogger->Write(FromDisplayManager, LogWarning, 
                "ST7789 display refresh requested but not implemented");
        }
        
        // When ST7789 is implemented, uncomment and use correct method:
        // m_pST7789Display->Refresh();  // or whatever method is appropriate
    }
}

void CDisplayManager::ShowButtonPress(unsigned nButtonIndex, const char* pButtonLabel)
{
    // Early validation - only proceed if we have a valid display
    if (m_pSH1106Display == nullptr && m_pST7789Display == nullptr)
    {
        return;
    }
    
    // Skip if no button label is provided
    if (pButtonLabel == nullptr)
    {
        return;
    }
    
    // For SH1106 display
    if (m_pSH1106Display != nullptr)
    {
        // Create a small notification at the bottom of the screen
        char notification[32];
        snprintf(notification, sizeof(notification), "Button: %s", pButtonLabel);
        
        // Draw at the bottom of the screen (clear that area first)
        m_pSH1106Display->DrawFilledRect(0, 56, 128, 8, SH1106_BLACK_COLOR);
        m_pSH1106Display->DrawText(0, 56, notification, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                 FALSE, FALSE, Font8x8);
        
        // Update the display immediately
        m_pSH1106Display->Refresh();
    }
    
    // For ST7789 display
    else if (m_pST7789Display != nullptr)
    {
        // Create a 2D graphics instance for drawing
        C2DGraphics graphics(m_pST7789Display);
        if (!graphics.Initialize())
        {
            m_pLogger->Write("dispman", LogError, "Failed to initialize 2D graphics");
            return;
        }
        
        // Store current screen
        // Not implemented: would require double buffering
        
        // Draw a message box with background preserving current screen as much as possible
        graphics.DrawRect(20, 100, 200, 50, COLOR2D(0, 80, 120));
        graphics.DrawRectOutline(20, 100, 200, 50, COLOR2D(255, 255, 255));
        
        // Format the message
        CString Message;
        Message.Format("Button %s pressed!", pButtonLabel);
        
        // Display the message in white on blue background
        graphics.DrawText(120, 125, COLOR2D(255, 255, 255), Message, C2DGraphics::AlignCenter);
        
        // Update the display
        graphics.UpdateDisplay();
        
        // In a real implementation, you would add a timer to restore the original screen
        // after a short delay. For now, this will remain until another screen is drawn.
    }
}

void CDisplayManager::ShowFileSelectionScreen(const char* pCurrentISOName, const char* pSelectedFileName, 
                                            unsigned CurrentFileIndex, unsigned TotalFiles)
{
    assert(pCurrentISOName != nullptr);
    assert(pSelectedFileName != nullptr);
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        // Implement SH1106 file selection screen
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            // Create a 2D graphics instance for drawing
            C2DGraphics graphics(m_pST7789Display);
            if (!graphics.Initialize())
            {
                m_pLogger->Write("dispman", LogError, "Failed to initialize 2D graphics");
                return;
            }
            
            // Clear the screen with WHITE background using the graphics object
            graphics.ClearScreen(COLOR2D(255, 255, 255));
            
            // Draw header bar with blue background
            graphics.DrawRect(0, 0, m_pST7789Display->GetWidth(), 30, COLOR2D(58, 124, 165));
            
            // 1. Changed header text to "Select an Image"
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), "Select Image:", C2DGraphics::AlignLeft);
            
            // 5. Moved counter to header area
            char position[16];
            snprintf(position, sizeof(position), "%u/%u", CurrentFileIndex, TotalFiles);
            graphics.DrawText(200, 8, COLOR2D(255, 255, 255), position, C2DGraphics::AlignRight);
            
            // 2. Replace "Current:" with CD icon and show current ISO name
            // Draw CD icon
            unsigned cd_x = 10;
            unsigned cd_y = 40;
            unsigned cd_radius = 10;
            
            // Draw outer circle of CD
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(0, 0, 0));
            
            // Draw middle circle of CD
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(0, 0, 0));
            
            // Draw center hole of CD
            graphics.DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(0, 0, 0));
            
            // Handle current ISO name (could be long)
            const size_t first_line_chars = 22; // Adjust based on screen width
            const size_t second_line_chars = 30; // For second line
            
            char first_line[32] = {0};
            char second_line[32] = {0};
            size_t iso_length = strlen(pCurrentISOName);
            
            if (iso_length <= first_line_chars)
            {
                // Short name fits on one line
                graphics.DrawText(35, 45, COLOR2D(0, 0, 0), pCurrentISOName, C2DGraphics::AlignLeft);
            }
            else
            {
                // First line starting on same line as CD icon
                strncpy(first_line, pCurrentISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                graphics.DrawText(35, 45, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                
                // Second line (potentially with ellipsis for very long names)
                if (iso_length > first_line_chars + second_line_chars - 4)
                {
                    // Very long name, use ellipsis and last portion
                    strncpy(second_line, pCurrentISOName + first_line_chars, second_line_chars - 17);
                    strcat(second_line, "...");
                    strcat(second_line, pCurrentISOName + iso_length - 13);
                }
                else
                {
                    strncpy(second_line, pCurrentISOName + first_line_chars, second_line_chars);
                    second_line[second_line_chars] = '\0';
                }
                
                second_line[sizeof(second_line) - 1] = '\0';
                graphics.DrawText(10, 65, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
            }
            
            // 3. Replace "Selected:" with a thick line across screen
            // Draw a thicker horizontal line (3 pixels thick)
            for (int i = 0; i < 3; i++)
            {
                graphics.DrawLine(0, 85 + i, m_pST7789Display->GetWidth(), 85 + i, COLOR2D(80, 80, 80));
            }
            
            // Draw the selected filename - allowing 3 lines in selection area
            const char* selected_file = pSelectedFileName;
            const size_t sel_max_chars = 30; // Increased character limit per line
            
            // Draw selection background
            graphics.DrawRect(5, 95, m_pST7789Display->GetWidth() - 10, 80, COLOR2D(0, 80, 120)); // Taller for 3 lines
            graphics.DrawRectOutline(5, 95, m_pST7789Display->GetWidth() - 10, 80, COLOR2D(255, 255, 255));
            
            // Fit selected filename across up to 3 lines
            if (strlen(selected_file) <= sel_max_chars)
            {
                // Short name fits on one line
                graphics.DrawText(10, 115, COLOR2D(255, 255, 255), selected_file, C2DGraphics::AlignLeft);
            }
            else if (strlen(selected_file) <= sel_max_chars * 2)
            {
                // Two lines needed
                char sel_line1[40], sel_line2[40];
                strncpy(sel_line1, selected_file, sel_max_chars);
                sel_line1[sel_max_chars] = '\0';
                
                strncpy(sel_line2, selected_file + sel_max_chars, sel_max_chars);
                sel_line2[sel_max_chars] = '\0';
                
                graphics.DrawText(10, 105, COLOR2D(255, 255, 255), sel_line1, C2DGraphics::AlignLeft);
                graphics.DrawText(10, 135, COLOR2D(255, 255, 255), sel_line2, C2DGraphics::AlignLeft);
            }
            else if (strlen(selected_file) <= sel_max_chars * 3)
            {
                // Three lines needed
                char sel_line1[40], sel_line2[40], sel_line3[40];
                strncpy(sel_line1, selected_file, sel_max_chars);
                sel_line1[sel_max_chars] = '\0';
                
                strncpy(sel_line2, selected_file + sel_max_chars, sel_max_chars);
                sel_line2[sel_max_chars] = '\0';
                
                strncpy(sel_line3, selected_file + (sel_max_chars * 2), sel_max_chars);
                sel_line3[sel_max_chars] = '\0';
                
                graphics.DrawText(10, 105, COLOR2D(255, 255, 255), sel_line1, C2DGraphics::AlignLeft);
                graphics.DrawText(10, 130, COLOR2D(255, 255, 255), sel_line2, C2DGraphics::AlignLeft);
                graphics.DrawText(10, 155, COLOR2D(255, 255, 255), sel_line3, C2DGraphics::AlignLeft);
            }
            else
            {
                // More than three lines, show first two lines and then ellipsis + end
                char sel_line1[40], sel_line2[40], sel_line3[40];
                strncpy(sel_line1, selected_file, sel_max_chars);
                sel_line1[sel_max_chars] = '\0';
                
                strncpy(sel_line2, selected_file + sel_max_chars, sel_max_chars);
                sel_line2[sel_max_chars] = '\0';
                
                // For third line, show "..." and the end part
                strcpy(sel_line3, "...");
                strcat(sel_line3, selected_file + strlen(selected_file) - (sel_max_chars - 5));
                
                graphics.DrawText(10, 105, COLOR2D(255, 255, 255), sel_line1, C2DGraphics::AlignLeft);
                graphics.DrawText(10, 130, COLOR2D(255, 255, 255), sel_line2, C2DGraphics::AlignLeft);
                graphics.DrawText(10, 155, COLOR2D(255, 255, 255), sel_line3, C2DGraphics::AlignLeft);
            }
            
            // Draw button bar at bottom
            graphics.DrawRect(0, 190, m_pST7789Display->GetWidth(), 50, COLOR2D(58, 124, 165));
            
            // 4 & 5. Updated navigation buttons with bigger arrows and icons
            
            // A button with LARGER UP arrow
            graphics.DrawText(12, 205, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignLeft);
            
            // Larger Up arrow
            unsigned arrow_x = 30;
            unsigned arrow_y = 205;
            graphics.DrawLine(arrow_x, arrow_y - 12, arrow_x, arrow_y, COLOR2D(255, 255, 255)); // Longer stem
            graphics.DrawLine(arrow_x - 6, arrow_y - 6, arrow_x, arrow_y - 12, COLOR2D(255, 255, 255)); // Wider head
            graphics.DrawLine(arrow_x + 6, arrow_y - 6, arrow_x, arrow_y - 12, COLOR2D(255, 255, 255));
            
            // B button with LARGER DOWN arrow
            graphics.DrawText(72, 205, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignLeft);
            
            // Larger Down arrow
            arrow_x = 90;
            arrow_y = 205;
            graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 12, COLOR2D(255, 255, 255)); // Longer stem
            graphics.DrawLine(arrow_x - 6, arrow_y + 6, arrow_x, arrow_y + 12, COLOR2D(255, 255, 255)); // Wider head
            graphics.DrawLine(arrow_x + 6, arrow_y + 6, arrow_x, arrow_y + 12, COLOR2D(255, 255, 255));
            
            // X button with RED X (Cancel)
            graphics.DrawText(132, 205, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignLeft);
            
            // Draw RED X
            unsigned x_center = 150;
            unsigned y_center = 205;
            // Red X (drawn with white on blue background)
            graphics.DrawLine(x_center - 8, y_center - 8, x_center + 8, y_center + 8, COLOR2D(255, 100, 100)); // Bright red-ish
            graphics.DrawLine(x_center + 8, y_center - 8, x_center - 8, y_center + 8, COLOR2D(255, 100, 100));
            // Thicker lines
            graphics.DrawLine(x_center - 7, y_center - 8, x_center + 9, y_center + 8, COLOR2D(255, 100, 100));
            graphics.DrawLine(x_center + 7, y_center - 8, x_center - 9, y_center + 8, COLOR2D(255, 100, 100));
            
            // Y button with GREEN CHECKMARK (OK)
            graphics.DrawText(192, 205, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignLeft);
            
            // Draw GREEN CHECKMARK
            unsigned check_x = 210;
            unsigned check_y = 205;
            // Green checkmark (drawn with white on blue background)
            graphics.DrawLine(check_x - 8, check_y, check_x - 3, check_y + 8, COLOR2D(100, 255, 100)); // Bright green-ish
            graphics.DrawLine(check_x - 3, check_y + 8, check_x + 8, check_y - 5, COLOR2D(100, 255, 100));
            // Thicker checkmark
            graphics.DrawLine(check_x - 8, check_y + 1, check_x - 3, check_y + 9, COLOR2D(100, 255, 100));
            graphics.DrawLine(check_x - 3, check_y + 9, check_x + 8, check_y - 4, COLOR2D(100, 255, 100));
            
            // Update the display
            graphics.UpdateDisplay();
            
            // Ensure display stays on
            m_pST7789Display->On();
        }
        break;
        
    default:
        break;
    }
}