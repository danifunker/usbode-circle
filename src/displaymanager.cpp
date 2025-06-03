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
            
            // Draw title text in white
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);
            
            // Draw WiFi icon as signal bars (3 bars) instead of antenna
            unsigned wifi_x = 10;
            unsigned wifi_y = 40;

            // Base of WiFi icon
            graphics.DrawRect(wifi_x + 8, wifi_y + 16, 4, 4, COLOR2D(0, 0, 0));

            // First (shortest) bar
            graphics.DrawRect(wifi_x + 7, wifi_y + 11, 6, 3, COLOR2D(0, 0, 0));

            // Second (medium) bar
            graphics.DrawRect(wifi_x + 4, wifi_y + 6, 12, 3, COLOR2D(0, 0, 0));

            // Third (longest) bar
            graphics.DrawRect(wifi_x + 1, wifi_y + 1, 18, 3, COLOR2D(0, 0, 0));
            
            // Draw IP address
            graphics.DrawText(35, 45, COLOR2D(0, 0, 0), pIPAddress, C2DGraphics::AlignLeft);
            
            // ALWAYS draw CD icon regardless of ISO status
            unsigned cd_x = 10;
            unsigned cd_y = 75;
            unsigned cd_radius = 10;
            
            // Draw outer circle of CD
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(0, 0, 0));
            
            // Draw middle circle of CD
            graphics.DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(0, 0, 0));
            
            // Draw center hole of CD
            graphics.DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(0, 0, 0));
            
            // ISO name handling with THREE-line support
            size_t first_line_chars = 25;  // First line chars
            size_t second_line_chars = 25; // Second line chars
            size_t third_line_chars = 25;  // Third line chars

            char first_line[40] = {0};
            char second_line[40] = {0};
            char third_line[40] = {0};
            size_t iso_length = strlen(pISOName);

            if (iso_length <= first_line_chars)
            {
                // Short name fits on one line
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), pISOName, C2DGraphics::AlignLeft);
            }
            else if (iso_length <= first_line_chars + second_line_chars)
            {
                // Two lines needed
                // First line
                strncpy(first_line, pISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                
                // Second line
                strncpy(second_line, pISOName + first_line_chars, second_line_chars);
                second_line[second_line_chars] = '\0';
                graphics.DrawText(35, 95, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
            }
            else if (iso_length <= first_line_chars + second_line_chars + third_line_chars)
            {
                // Three lines needed
                // First line
                strncpy(first_line, pISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                
                // Second line
                strncpy(second_line, pISOName + first_line_chars, second_line_chars);
                second_line[second_line_chars] = '\0';
                graphics.DrawText(35, 95, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
                
                // Third line
                strncpy(third_line, pISOName + first_line_chars + second_line_chars, third_line_chars);
                third_line[third_line_chars] = '\0';
                graphics.DrawText(35, 115, COLOR2D(0, 0, 0), third_line, C2DGraphics::AlignLeft);
            }
            else
            {
                // More than three lines worth of text - show first two lines and end with ellipsis + last part
                // First line
                strncpy(first_line, pISOName, first_line_chars);
                first_line[first_line_chars] = '\0';
                graphics.DrawText(35, 75, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                
                // Second line
                strncpy(second_line, pISOName + first_line_chars, second_line_chars);
                second_line[second_line_chars] = '\0';
                graphics.DrawText(35, 95, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
                
                // Third line with "..." and last 11 chars of filename
                strcpy(third_line, "...");
                strcat(third_line, pISOName + (iso_length - 11));
                graphics.DrawText(35, 115, COLOR2D(0, 0, 0), third_line, C2DGraphics::AlignLeft);
            }
            
            // Move the USB icon further down to accommodate 3 lines of text and make it larger
            unsigned usb_x = 10;
            unsigned usb_y = 170; // Moved down close to nav bar

            // USB outline - rectangular shape (3x larger)
            // Using DrawRect for thicker lines instead of individual pixels
            graphics.DrawRect(usb_x, usb_y, 24, 2, COLOR2D(0, 0, 0)); // Top horizontal
            graphics.DrawRect(usb_x, usb_y+21, 24, 2, COLOR2D(0, 0, 0)); // Bottom horizontal
            graphics.DrawRect(usb_x, usb_y, 2, 23, COLOR2D(0, 0, 0)); // Left vertical
            graphics.DrawRect(usb_x+22, usb_y, 2, 23, COLOR2D(0, 0, 0)); // Right vertical

            // USB pins (larger)
            graphics.DrawRect(usb_x+6, usb_y+6, 4, 12, COLOR2D(0, 0, 0)); // Left pin
            graphics.DrawRect(usb_x+14, usb_y+6, 4, 12, COLOR2D(0, 0, 0)); // Right pin

            // Draw USB speed info next to the USB icon
            graphics.DrawText(40, 180, COLOR2D(0, 0, 0), pUSBSpeed, C2DGraphics::AlignLeft);
            
            // Use the helper function to draw navigation bar (false = main screen layout)
            DrawNavigationBar(graphics, "main");
            
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

void CDisplayManager::ShowFileSelectionScreen(const char* pCurrentISOName, const char* pSelectedFileName, 
                                            unsigned CurrentFileIndex, unsigned TotalFiles)
{
    assert(pCurrentISOName != nullptr);
    assert(pSelectedFileName != nullptr);
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw header - "Select ISO:"
            m_pSH1106Display->DrawText(0, 0, "Select ISO:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Draw current ISO information
            m_pSH1106Display->DrawText(0, 10, "Current:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Show current ISO name (truncate if too long)
            const size_t max_cur_chars = 18;
            char current_iso[24] = {0};
            
            if (strlen(pCurrentISOName) <= max_cur_chars) {
                m_pSH1106Display->DrawText(0, 20, pCurrentISOName, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                         FALSE, FALSE, Font6x7);
            } else {
                // Truncate with ellipsis
                strncpy(current_iso, pCurrentISOName, max_cur_chars - 3);
                strcat(current_iso, "...");
                m_pSH1106Display->DrawText(0, 20, current_iso, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                         FALSE, FALSE, Font6x7);
            }
            
            // Draw selected ISO info (with highlighting)
            m_pSH1106Display->DrawText(0, 30, "Selected:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Show selected filename in highlighted box
            // First draw the box
            for (int y = 40; y < 50; y++) {
                for (int x = 0; x < m_pSH1106Display->GetWidth(); x++) {
                    m_pSH1106Display->SetPixel(x, y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
                }
            }
            
            // Truncate selected filename if needed
            const size_t max_sel_chars = 16;
            char selected[24] = {0};
            
            if (strlen(pSelectedFileName) <= max_sel_chars) {
                m_pSH1106Display->DrawText(4, 42, pSelectedFileName, SH1106_BLACK_COLOR, SH1106_WHITE_COLOR,
                                         FALSE, FALSE, Font6x7);
            } else {
                // Truncate with ellipsis
                strncpy(selected, pSelectedFileName, max_sel_chars - 3);
                strcat(selected, "...");
                m_pSH1106Display->DrawText(4, 42, selected, SH1106_BLACK_COLOR, SH1106_WHITE_COLOR,
                                         FALSE, FALSE, Font6x7);
            }
            
            // Draw navigation info
            char position[16];
            snprintf(position, sizeof(position), "%u/%u", CurrentFileIndex, TotalFiles);
            m_pSH1106Display->DrawText(0, 56, position, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Draw buttons
            m_pSH1106Display->DrawText(64, 56, "UP/DN/X/Y", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Ensure the display is updated
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
            
            // Draw title text in white - "Select an Image"
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), "Select Image:", C2DGraphics::AlignLeft);
            
            // Move counter to header area
            char position[16];
            snprintf(position, sizeof(position), "%u/%u", CurrentFileIndex, TotalFiles);
            graphics.DrawText(200, 8, COLOR2D(255, 255, 255), position, C2DGraphics::AlignRight);
            
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
            // Start with full string display and then truncate if needed
            if (strlen(pCurrentISOName) == 0) {
                // Handle empty current ISO name
                graphics.DrawText(35, 45, COLOR2D(0, 0, 0), "No image loaded", C2DGraphics::AlignLeft);
            } else {
                // Improved current filename display
                const size_t first_line_chars = 22;
                const size_t second_line_chars = 30;
                
                char first_line[40] = {0};
                char second_line[40] = {0};
                size_t iso_length = strlen(pCurrentISOName);
                
                if (iso_length <= first_line_chars) {
                    // Short name fits on one line
                    graphics.DrawText(35, 45, COLOR2D(0, 0, 0), pCurrentISOName, C2DGraphics::AlignLeft);
                } else {
                    // First line starting on same line as CD icon
                    strncpy(first_line, pCurrentISOName, first_line_chars);
                    first_line[first_line_chars] = '\0';
                    graphics.DrawText(35, 45, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);
                    
                    // Second line - show remainder of text
                    if (iso_length > first_line_chars + second_line_chars) {
                        // Very long filename, truncate middle
                        // First part (12 chars)
                        strncpy(second_line, pCurrentISOName + first_line_chars, 12);
                        second_line[12] = '\0';
                        // Add ellipsis
                        strcat(second_line, "...");
                        // Last part (12 chars)
                        strncat(second_line, 
                                pCurrentISOName + iso_length - 12,
                                12);
                    } else {
                        // Just display remainder
                        strncpy(second_line, pCurrentISOName + first_line_chars, second_line_chars);
                        second_line[second_line_chars] = '\0';
                    }
                    
                    second_line[sizeof(second_line) - 1] = '\0';
                    graphics.DrawText(10, 65, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
                }
            }
            
            // Draw a thicker horizontal line (3 pixels thick)
            for (int i = 0; i < 3; i++) {
                graphics.DrawLine(0, 85 + i, m_pST7789Display->GetWidth(), 85 + i, COLOR2D(80, 80, 80));
            }
            
            // Draw selection background
            graphics.DrawRect(5, 95, m_pST7789Display->GetWidth() - 10, 80, COLOR2D(0, 80, 120));
            graphics.DrawRectOutline(5, 95, m_pST7789Display->GetWidth() - 10, 80, COLOR2D(255, 255, 255));
            
            // Handle selected filename display
            if (strlen(pSelectedFileName) == 0) {
                // Handle empty selection
                graphics.DrawText(10, 135, COLOR2D(255, 255, 255), "No files found", C2DGraphics::AlignCenter);
            } else {
                // Get actual filename length
                size_t filename_len = strlen(pSelectedFileName);
                
                // Split filename into three lines if needed
                // Use smaller chunks to ensure more of the filename is visible
                size_t chars_per_line = 28;
                
                if (filename_len <= chars_per_line) {
                    // Short filename fits on one line
                    graphics.DrawText(10, 135, COLOR2D(255, 255, 255), pSelectedFileName, C2DGraphics::AlignLeft);
                } 
                else if (filename_len <= chars_per_line * 2) {
                    // Medium filename fits on two lines
                    char line1[40] = {0};
                    char line2[40] = {0};
                    
                    // First line
                    strncpy(line1, pSelectedFileName, chars_per_line);
                    line1[chars_per_line] = '\0';
                    
                    // Second line
                    strncpy(line2, pSelectedFileName + chars_per_line, chars_per_line);
                    line2[chars_per_line] = '\0';
                    
                    graphics.DrawText(10, 120, COLOR2D(255, 255, 255), line1, C2DGraphics::AlignLeft);
                    graphics.DrawText(10, 145, COLOR2D(255, 255, 255), line2, C2DGraphics::AlignLeft);
                }
                else if (filename_len <= chars_per_line * 3) {
                    // Long filename fits on three lines
                    char line1[40] = {0};
                    char line2[40] = {0};
                    char line3[40] = {0};
                    
                    // First line
                    strncpy(line1, pSelectedFileName, chars_per_line);
                    line1[chars_per_line] = '\0';
                    
                    // Second line
                    strncpy(line2, pSelectedFileName + chars_per_line, chars_per_line);
                    line2[chars_per_line] = '\0';
                    
                    // Third line
                    strncpy(line3, pSelectedFileName + (chars_per_line * 2), chars_per_line);
                    line3[chars_per_line] = '\0';
                    
                    graphics.DrawText(10, 110, COLOR2D(255, 255, 255), line1, C2DGraphics::AlignLeft);
                    graphics.DrawText(10, 135, COLOR2D(255, 255, 255), line2, C2DGraphics::AlignLeft);
                    graphics.DrawText(10, 160, COLOR2D(255, 255, 255), line3, C2DGraphics::AlignLeft);
                }
                else {
                    // Very long filename - show first two lines and last part
                    char line1[40] = {0};
                    char line2[40] = {0};
                    char line3[40] = {0};
                    
                    // First line
                    strncpy(line1, pSelectedFileName, chars_per_line);
                    line1[chars_per_line] = '\0';
                    
                    // Second line
                    strncpy(line2, pSelectedFileName + chars_per_line, chars_per_line);
                    line2[chars_per_line] = '\0';
                    
                    // Third line - show last part with ellipsis
                    strcpy(line3, "...");
                    strncat(line3, 
                           pSelectedFileName + (filename_len - (chars_per_line - 3)),
                           chars_per_line - 3);
                    
                    graphics.DrawText(10, 110, COLOR2D(255, 255, 255), line1, C2DGraphics::AlignLeft);
                    graphics.DrawText(10, 135, COLOR2D(255, 255, 255), line2, C2DGraphics::AlignLeft);
                    graphics.DrawText(10, 160, COLOR2D(255, 255, 255), line3, C2DGraphics::AlignLeft);
                }
            }
            
            // Use the helper function to draw navigation bar
            DrawNavigationBar(graphics, "selection");
            
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

void CDisplayManager::Refresh(void)
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            m_pSH1106Display->Refresh();
        }
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            // For ST7789, we need to refresh using the graphics object
            C2DGraphics graphics(m_pST7789Display);
            if (graphics.Initialize())
            {
                graphics.UpdateDisplay();
            }
        }
        break;
        
    default:
        break;
    }
}

void CDisplayManager::ShowButtonPress(unsigned nButtonIndex, const char* pButtonLabel)
{
    // Currently not implemented
    // This would show a brief button press indicator on the screen
}

// Update the helper function to make button labels more visible
void CDisplayManager::DrawNavigationBar(C2DGraphics& graphics, const char* screenType)
{
    // Draw button bar at bottom
    graphics.DrawRect(0, 210, graphics.GetWidth(), 30, COLOR2D(58, 124, 165));
    
    // --- A BUTTON ---
    // Draw a more visible button with border - use a lighter gray for better contrast
    graphics.DrawRect(5, 215, 18, 20, COLOR2D(150, 150, 150));
    graphics.DrawRectOutline(5, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
    
    // A button label - make it larger and thicker by drawing it multiple times with slight offsets
    graphics.DrawText(11, 225, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignCenter);
    graphics.DrawText(12, 225, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignCenter); // Thicker
    graphics.DrawText(11, 226, COLOR2D(255, 255, 255), "A", C2DGraphics::AlignCenter); // Thicker
    
    // Thicker Up arrow - positioned after the button - make larger by 1px
    unsigned arrow_x = 35;
    unsigned arrow_y = 225;
    
    // Stem (3px thick)
    graphics.DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255)); // 1px longer
    graphics.DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255)); // Added 3rd line
    
    // Left side of arrowhead (3px thick) - 1px larger
    graphics.DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255)); 
    graphics.DrawLine(arrow_x - 7, arrow_y - 5, arrow_x - 1, arrow_y - 12, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x - 6, arrow_y - 7, arrow_x + 1, arrow_y - 13, COLOR2D(255, 255, 255));
    
    // Right side of arrowhead (3px thick) - 1px larger
    graphics.DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 7, arrow_y - 5, arrow_x + 1, arrow_y - 12, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 6, arrow_y - 7, arrow_x - 1, arrow_y - 13, COLOR2D(255, 255, 255));
    
    // --- B BUTTON ---
    // Draw a more visible button with border - use a lighter gray for better contrast
    graphics.DrawRect(65, 215, 18, 20, COLOR2D(150, 150, 150));
    graphics.DrawRectOutline(65, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
    
    // B button label - make it larger and thicker by drawing it multiple times with slight offsets
    graphics.DrawText(71, 225, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignCenter);
    graphics.DrawText(72, 225, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignCenter); // Thicker
    graphics.DrawText(71, 226, COLOR2D(255, 255, 255), "B", C2DGraphics::AlignCenter); // Thicker
    
    // Thicker Down arrow - positioned after the button - make larger by 1px
    arrow_x = 95;
    arrow_y = 225;
    
    // Stem (3px thick)
    graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255)); // 1px longer
    graphics.DrawLine(arrow_x - 1, arrow_y, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 1, arrow_y, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255)); // Added 3rd line
    
    // Left side of arrowhead (3px thick) - 1px larger
    graphics.DrawLine(arrow_x - 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x - 7, arrow_y + 5, arrow_x - 1, arrow_y + 12, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x - 6, arrow_y + 7, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255));
    
    // Right side of arrowhead (3px thick) - 1px larger
    graphics.DrawLine(arrow_x + 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 7, arrow_y + 5, arrow_x + 1, arrow_y + 12, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 6, arrow_y + 7, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
    
    // Compare strings to determine which screen type we're displaying
    if (strcmp(screenType, "main") == 0) {
        // --- X BUTTON ---
        // Draw a more visible button with border - use a lighter gray for better contrast
        graphics.DrawRect(125, 215, 18, 20, COLOR2D(150, 150, 150));
        graphics.DrawRectOutline(125, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
        
        // X button label - make it larger and thicker by drawing it multiple times with slight offsets
        graphics.DrawText(131, 225, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter);
        graphics.DrawText(132, 225, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter); // Thicker
        graphics.DrawText(131, 226, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter); // Thicker
        
        // Draw 3 horizontal bars for menu - positioned after the button
        unsigned menu_x = 155;
        unsigned menu_y = 220;
        // Make bars thicker (2px)
        graphics.DrawLine(menu_x, menu_y, menu_x + 15, menu_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(menu_x, menu_y + 1, menu_x + 15, menu_y + 1, COLOR2D(255, 255, 255));
        
        graphics.DrawLine(menu_x, menu_y + 5, menu_x + 15, menu_y + 5, COLOR2D(255, 255, 255));
        graphics.DrawLine(menu_x, menu_y + 6, menu_x + 15, menu_y + 6, COLOR2D(255, 255, 255));
        
        graphics.DrawLine(menu_x, menu_y + 10, menu_x + 15, menu_y + 10, COLOR2D(255, 255, 255));
        graphics.DrawLine(menu_x, menu_y + 11, menu_x + 15, menu_y + 11, COLOR2D(255, 255, 255));
        
        // --- Y BUTTON ---
        // Draw a more visible button with border - use a lighter gray for better contrast
        graphics.DrawRect(185, 215, 18, 20, COLOR2D(150, 150, 150));
        graphics.DrawRectOutline(185, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
        
        // Y button label - make it larger and thicker by drawing it multiple times with slight offsets
        graphics.DrawText(191, 225, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter);
        graphics.DrawText(192, 225, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter); // Thicker
        graphics.DrawText(191, 226, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter); // Thicker
        
        // Draw folder icon - positioned after the button
        unsigned folder_x = 215;
        unsigned folder_y = 220;
        
        // Folder base - make thicker
        graphics.DrawRect(folder_x, folder_y + 3, 16, 11, COLOR2D(255, 255, 255)); // 1px taller
        
        // Folder tab - make thicker
        graphics.DrawRect(folder_x + 2, folder_y, 8, 4, COLOR2D(255, 255, 255)); // 1px taller
    } 
    else if (strcmp(screenType, "selection") == 0 || strcmp(screenType, "advanced") == 0) {
        // --- X BUTTON ---
        // Draw a more visible button with border - use a lighter gray for better contrast
        graphics.DrawRect(125, 215, 18, 20, COLOR2D(150, 150, 150));
        graphics.DrawRectOutline(125, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
        
        // X button label - make it larger and thicker by drawing it multiple times with slight offsets
        graphics.DrawText(131, 225, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter);
        graphics.DrawText(132, 225, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter); // Thicker
        graphics.DrawText(131, 226, COLOR2D(255, 255, 255), "X", C2DGraphics::AlignCenter); // Thicker
        
        // Draw RED X - positioned after the button - 1px larger
        unsigned x_center = 155;
        unsigned y_center = 225;
        
        // Red X (drawn with red on blue background) - make it thicker
        graphics.DrawLine(x_center - 9, y_center - 9, x_center + 9, y_center + 9, COLOR2D(255, 100, 100)); // 1px larger
        graphics.DrawLine(x_center + 9, y_center - 9, x_center - 9, y_center + 9, COLOR2D(255, 100, 100)); // 1px larger
        
        // Thicker lines - add more lines for thickness
        graphics.DrawLine(x_center - 8, y_center - 9, x_center + 9, y_center + 8, COLOR2D(255, 100, 100));
        graphics.DrawLine(x_center + 8, y_center - 9, x_center - 9, y_center + 8, COLOR2D(255, 100, 100));
        graphics.DrawLine(x_center - 9, y_center - 8, x_center + 8, y_center + 9, COLOR2D(255, 100, 100));
        graphics.DrawLine(x_center + 9, y_center - 8, x_center - 8, y_center + 9, COLOR2D(255, 100, 100));
        
        // --- Y BUTTON ---
        // Draw a more visible button with border - use a lighter gray for better contrast
        graphics.DrawRect(185, 215, 18, 20, COLOR2D(150, 150, 150));
        graphics.DrawRectOutline(185, 215, 18, 20, COLOR2D(255, 255, 255)); // White border
        
        // Y button label - make it larger and thicker by drawing it multiple times with slight offsets
        graphics.DrawText(191, 225, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter);
        graphics.DrawText(192, 225, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter); // Thicker
        graphics.DrawText(191, 226, COLOR2D(255, 255, 255), "Y", C2DGraphics::AlignCenter); // Thicker
        
        // Draw GREEN CHECKMARK - positioned after the button - 1px larger
        unsigned check_x = 215;
        unsigned check_y = 225;
        
        // Green checkmark (drawn with green on blue background) - make it thicker and 1px larger
        graphics.DrawLine(check_x - 9, check_y, check_x - 3, check_y + 9, COLOR2D(100, 255, 100)); // 1px larger
        graphics.DrawLine(check_x - 3, check_y + 9, check_x + 9, check_y - 6, COLOR2D(100, 255, 100)); // 1px larger
        
        // Thicker checkmark - add more lines
        graphics.DrawLine(check_x - 9, check_y + 1, check_x - 3, check_y + 10, COLOR2D(100, 255, 100));
        graphics.DrawLine(check_x - 3, check_y + 10, check_x + 9, check_y - 5, COLOR2D(100, 255, 100));
        graphics.DrawLine(check_x - 8, check_y - 1, check_x - 2, check_y + 9, COLOR2D(100, 255, 100));
        graphics.DrawLine(check_x - 2, check_y + 9, check_x + 9, check_y - 7, COLOR2D(100, 255, 100));
    }
    // You can add more screen types here with else if statements
}

void CDisplayManager::ShowAdvancedScreen(void)
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        // Implement SH1106 advanced screen if needed
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
            
            // Draw title text in white
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), "Advanced Menu", C2DGraphics::AlignLeft);
            
            // Use the helper function to draw navigation bar
            DrawNavigationBar(graphics, "advanced");
            
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