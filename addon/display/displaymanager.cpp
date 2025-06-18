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
#include <linux/kernel.h>
#include <circle/time.h>

#include <assert.h>

static const char FromDisplayManager[] = "dispman";

// Updated constructor that takes display type directly
CDisplayManager::CDisplayManager(CLogger *pLogger, TDisplayType DisplayType, unsigned nScreenTimeoutSeconds)
    : m_pLogger(pLogger),
      m_DisplayType(DisplayType),
      m_pSH1106Display(nullptr),
      m_pSH1106Device(nullptr),
      m_pST7789Display(nullptr),
      m_pST7789Device(nullptr),
      m_nScreenTimeoutSeconds(nScreenTimeoutSeconds),
      m_nLastActivityTime(0),
      m_bScreenActive(true),
      m_bTimeoutWarningShown(false),
      m_bMainScreenActive(true)
{
    assert(m_pLogger != nullptr);
    
    // Ensure minimum timeout of 3 seconds to allow for warning
    if (m_nScreenTimeoutSeconds < 3) {
        m_nScreenTimeoutSeconds = 3;
    }
    
    // Initialize the last activity time to now
    m_nLastActivityTime = CTimer::Get()->GetTicks();
    
    m_pLogger->Write("dispman", LogNotice, 
                  "Screen timeout initialized to %u seconds", m_nScreenTimeoutSeconds);
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
    // Don't update if screen should be sleeping
    if (!ShouldAllowDisplayUpdates()) {
        return;
    }
    
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
    // Don't update if screen should be sleeping
    if (!ShouldAllowDisplayUpdates()) {
        return;
    }
    
    assert(pCurrentISOName != nullptr);
    assert(pSelectedFileName != nullptr);
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw title at the top - moved down slightly
            m_pSH1106Display->DrawText(0, 2, "Select Image:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Use the current ISO passed as parameter
            const char* currentImage = pCurrentISOName;
            
            // Draw CD icon (replacing "I: " text)
            unsigned int cd_x = 0;
            unsigned int cd_y = 12;
            
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
            
            // First line has CD icon + space, so fewer characters per line
            const size_t first_line_chars = 18;
            // Second line and selection lines have no prefix, so can use more characters
            const size_t chars_per_line = 21;
            
            // Calculate line y position based on current ISO length
            unsigned int line_y;
            
            // CURRENT ISO (Top of screen) =========================
            if (strlen(currentImage) <= first_line_chars)
            {
                // Short name fits on one line - use direct text copy instead of format strings
                m_pSH1106Display->DrawText(12, 12, currentImage, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                line_y = 22;
            }
            else
            {
                // First line with CD icon - use safe string handling
                char first_line[32];
                memset(first_line, 0, sizeof(first_line));
                strncpy(first_line, currentImage, first_line_chars);
                first_line[first_line_chars] = '\0';
                
                m_pSH1106Display->DrawText(12, 12, first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Second line handling for very long names
                char second_line[32];
                memset(second_line, 0, sizeof(second_line));
                
                if (strlen(currentImage) > first_line_chars + chars_per_line - 14)  // Changed from -12 to -14
                {
                    // Very long name, ensure last 11 chars are visible
                    size_t remaining_chars = chars_per_line - 14;  // Changed from -12 to -14
                    
                    // Copy first part with explicit termination
                    strncpy(second_line, currentImage + first_line_chars, remaining_chars);
                    second_line[remaining_chars] = '\0';
                    
                    // Add three periods instead of ellipsis character and ensure the last 11 chars
                    strcat(second_line, "...");
                    strcat(second_line, currentImage + strlen(currentImage) - 11);  // Keep last 11 chars
                }
                else
                {
                    // Just copy the remaining part with explicit termination
                    strncpy(second_line, currentImage + first_line_chars, chars_per_line);
                    second_line[chars_per_line] = '\0';
                }
                
                m_pSH1106Display->DrawText(0, 22, second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                line_y = 32;
            }
            
            // Draw divider line
            for (unsigned int x = 0; x < 128; x++)
            {
                m_pSH1106Display->SetPixel(x, line_y, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // SELECTED ISO (Bottom of screen) =========================
            const char* selected_file = pSelectedFileName;
            
            // Position for new selection depends on line_y
            unsigned int selection_y = line_y + 3;
            
            // === Support up to THREE lines for selected file ===
            if (strlen(selected_file) <= chars_per_line)
            {
                // Short name fits on one line
                m_pSH1106Display->DrawText(0, selection_y, selected_file, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
            }
            else if (strlen(selected_file) <= chars_per_line * 2)
            {
                // First line of selection
                char sel_first_line[32];
                strncpy(sel_first_line, selected_file, chars_per_line);
                sel_first_line[chars_per_line] = '\0';
                
                m_pSH1106Display->DrawText(0, selection_y, sel_first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Second line - remaining text
                char sel_second_line[32];
                strncpy(sel_second_line, selected_file + chars_per_line, chars_per_line);
                sel_second_line[chars_per_line] = '\0';
                
                m_pSH1106Display->DrawText(0, selection_y + 10, sel_second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
            }
            else
            {
                // Very long filename needs three lines or ellipsis
                // First line of selection
                char sel_first_line[32];
                strncpy(sel_first_line, selected_file, chars_per_line);
                sel_first_line[chars_per_line] = '\0';
                
                m_pSH1106Display->DrawText(0, selection_y, sel_first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Second line 
                char sel_second_line[32];
                strncpy(sel_second_line, selected_file + chars_per_line, chars_per_line);
                sel_second_line[chars_per_line] = '\0';
                
                m_pSH1106Display->DrawText(0, selection_y + 10, sel_second_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Third line - always show last 11 chars with ellipsis
                char sel_third_line[32] = "...";
                strcat(sel_third_line, selected_file + strlen(selected_file) - 11);
                
                m_pSH1106Display->DrawText(0, selection_y + 20, sel_third_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
            }
            
            // Draw position indicator for file selection
            char position[16];
            snprintf(position, sizeof(position), "%u/%u", CurrentFileIndex, TotalFiles);
            int posWidth = strlen(position) * 6; // Approximate width of text
            
            // Move position indicator up to avoid getting cut off
            m_pSH1106Display->DrawText(128 - posWidth, 55, position, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Ensure the display is updated
            m_pSH1106Display->Refresh();
            
            m_pLogger->Write("display", LogNotice, "File selection screen updated");
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
    // Don't refresh if screen should be sleeping
    if (!ShouldAllowDisplayUpdates()) {
        return;
    }
    
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

// Modified function to draw button letters directly using lines
void CDisplayManager::DrawNavigationBar(C2DGraphics& graphics, const char* screenType)
{
    // Draw button bar at bottom
    graphics.DrawRect(0, 210, graphics.GetWidth(), 30, COLOR2D(58, 124, 165));
    
    // --- A BUTTON ---
    // Draw a white button with dark border for better contrast
    graphics.DrawRect(5, 215, 18, 20, COLOR2D(255, 255, 255));
    graphics.DrawRectOutline(5, 215, 18, 20, COLOR2D(0, 0, 0));
    
    // Draw letter "A" using lines instead of text
    unsigned a_x = 14; // Center of A
    unsigned a_y = 225; // Center of button
    
    // Draw A using thick lines (3px wide)
    // Left diagonal of A
    graphics.DrawLine(a_x - 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(a_x - 5, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(a_x - 3, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));
    
    // Right diagonal of A
    graphics.DrawLine(a_x + 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(a_x + 5, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(a_x + 3, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));
    
    // Middle bar of A
    graphics.DrawLine(a_x - 2, a_y, a_x + 2, a_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(a_x - 2, a_y + 1, a_x + 2, a_y + 1, COLOR2D(0, 0, 0)); // Fixed: a_y+1 instead of a_x+1
    
    // UP arrow for navigation screens or custom icon for main screen
    unsigned arrow_x = 35;
    unsigned arrow_y = 225;
    
    if (strcmp(screenType, "main") == 0) {
        // On main screen, show select icon
        // Stem (3px thick)
        graphics.DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));
        
        // Arrow head
        graphics.DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    } else {
        // On other screens, show up navigation arrow
        // Stem (3px thick)
        graphics.DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));
        
        // Arrow head
        graphics.DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        graphics.DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    }
    
    // --- B BUTTON ---
    // Draw a white button with dark border for better contrast
    graphics.DrawRect(65, 215, 18, 20, COLOR2D(255, 255, 255));
    graphics.DrawRectOutline(65, 215, 18, 20, COLOR2D(0, 0, 0));
    
    // Draw letter "B" using lines instead of text
    unsigned b_x = 74; // Center of B
    unsigned b_y = 225; // Center of button
    
    // Draw B using thick lines
    // Vertical line of B
    graphics.DrawLine(b_x - 3, b_y - 6, b_x - 3, b_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x - 2, b_y - 6, b_x - 2, b_y + 6, COLOR2D(0, 0, 0));
    
    // Top curve of B
    graphics.DrawLine(b_x - 3, b_y - 6, b_x + 2, b_y - 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 2, b_y - 6, b_x + 3, b_y - 5, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 3, b_y - 5, b_x + 3, b_y - 1, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 3, b_y - 1, b_x + 2, b_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 2, b_y, b_x - 2, b_y, COLOR2D(0, 0, 0));
    
    // Bottom curve of B
    graphics.DrawLine(b_x - 3, b_y + 6, b_x + 2, b_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 2, b_y + 6, b_x + 3, b_y + 5, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 3, b_y + 5, b_x + 3, b_y + 1, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x + 3, b_y + 1, b_x + 2, b_y, COLOR2D(0, 0, 0));
    
    // Thicker parts - reinforce
    graphics.DrawLine(b_x - 1, b_y - 5, b_x + 1, b_y - 5, COLOR2D(0, 0, 0));
    graphics.DrawLine(b_x - 1, b_y + 5, b_x + 1, b_y + 5, COLOR2D(0, 0, 0));
    
    // Down arrow for all screens
    arrow_x = 95;
    arrow_y = 225;
    
    // Stem (3px thick)
    graphics.DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x - 1, arrow_y, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 1, arrow_y, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255));
    
    // Arrow head
    graphics.DrawLine(arrow_x - 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    graphics.DrawLine(arrow_x + 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    
    // --- X BUTTON ---
    // Draw a white button with dark border for better contrast
    graphics.DrawRect(125, 215, 18, 20, COLOR2D(255, 255, 255));
    graphics.DrawRectOutline(125, 215, 18, 20, COLOR2D(0, 0, 0));
    
    // Draw letter "X" using lines instead of text
    unsigned x_x = 134; // Center of X
    unsigned x_y = 225; // Center of button
    
    // Draw X using thick lines (3px wide)
    // First diagonal of X (top-left to bottom-right)
    graphics.DrawLine(x_x - 4, x_y - 6, x_x + 4, x_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(x_x - 5, x_y - 6, x_x + 3, x_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(x_x - 3, x_y - 6, x_x + 5, x_y + 6, COLOR2D(0, 0, 0));
    
    // Second diagonal of X (top-right to bottom-left)
    graphics.DrawLine(x_x + 4, x_y - 6, x_x - 4, x_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(x_x + 5, x_y - 6, x_x - 3, x_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(x_x + 3, x_y - 6, x_x - 5, x_y + 6, COLOR2D(0, 0, 0));
    
    // Icon next to X button - different based on screen type
    unsigned icon_x = 155;
    unsigned icon_y = 225;
    
    if (strcmp(screenType, "main") == 0) {
        // Menu bars for main screen
        // Thicker menu bars (2px)
        graphics.DrawLine(icon_x, icon_y - 5, icon_x + 15, icon_y - 5, COLOR2D(255, 255, 255));
        graphics.DrawLine(icon_x, icon_y - 4, icon_x + 15, icon_y - 4, COLOR2D(255, 255, 255));
        
        graphics.DrawLine(icon_x, icon_y, icon_x + 15, icon_y, COLOR2D(255, 255, 255));
        graphics.DrawLine(icon_x, icon_y + 1, icon_x + 15, icon_y + 1, COLOR2D(255, 255, 255));
        
        graphics.DrawLine(icon_x, icon_y + 5, icon_x + 15, icon_y + 5, COLOR2D(255, 255, 255));
        graphics.DrawLine(icon_x, icon_y + 6, icon_x + 15, icon_y + 6, COLOR2D(255, 255, 255));
    } else {
        // Red X icon for other screens (cancel)
        graphics.DrawLine(icon_x - 8, icon_y - 8, icon_x + 8, icon_y + 8, COLOR2D(255, 0, 0));
        graphics.DrawLine(icon_x + 8, icon_y - 8, icon_x - 8, icon_y + 8, COLOR2D(255, 0, 0));
        
        // Make red X thicker
        graphics.DrawLine(icon_x - 7, icon_y - 8, icon_x + 7, icon_y + 8, COLOR2D(255, 0, 0));
        graphics.DrawLine(icon_x + 7, icon_y - 8, icon_x - 7, icon_y + 8, COLOR2D(255, 0, 0));
        graphics.DrawLine(icon_x - 8, icon_y - 7, icon_x + 8, icon_y + 7, COLOR2D(255, 0, 0));
        graphics.DrawLine(icon_x + 8, icon_y - 7, icon_x - 8, icon_y + 7, COLOR2D(255, 0, 0));
    }
    
    // --- Y BUTTON ---
    // Draw a white button with dark border for better contrast
    graphics.DrawRect(185, 215, 18, 20, COLOR2D(255, 255, 255));
    graphics.DrawRectOutline(185, 215, 18, 20, COLOR2D(0, 0, 0));
    
    // Draw letter "Y" using lines instead of text
    unsigned y_x = 194; // Center of Y
    unsigned y_y = 225; // Center of button
    
    // Draw Y using thick lines (3px wide)
    // Upper left diagonal of Y
    graphics.DrawLine(y_x - 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x - 5, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x - 3, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));
    
    // Upper right diagonal of Y
    graphics.DrawLine(y_x + 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x + 5, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x + 3, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));
    
    // Stem of Y
    graphics.DrawLine(y_x, y_y, y_x, y_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x - 1, y_y, y_x - 1, y_y + 6, COLOR2D(0, 0, 0));
    graphics.DrawLine(y_x + 1, y_y, y_x + 1, y_y + 6, COLOR2D(0, 0, 0));
    
    // Icon next to Y button - different based on screen type
    unsigned y_icon_x = 215;
    unsigned y_icon_y = 225;
    
    if (strcmp(screenType, "main") == 0) {
        // Folder icon for main screen
        graphics.DrawRect(y_icon_x, y_icon_y - 2, 16, 11, COLOR2D(255, 255, 255));
        graphics.DrawRect(y_icon_x + 2, y_icon_y - 5, 8, 4, COLOR2D(255, 255, 255));
    } else {
        // GREEN CHECKMARK for all other screens
        // Draw a green checkmark
        // Shorter part of checkmark
        graphics.DrawLine(y_icon_x - 8, y_icon_y, y_icon_x - 3, y_icon_y + 5, COLOR2D(0, 255, 0));
        graphics.DrawLine(y_icon_x - 8, y_icon_y + 1, y_icon_x - 3, y_icon_y + 6, COLOR2D(0, 255, 0));
        graphics.DrawLine(y_icon_x - 7, y_icon_y, y_icon_x - 2, y_icon_y + 5, COLOR2D(0, 255, 0));
        
        // Longer part of checkmark
        graphics.DrawLine(y_icon_x - 3, y_icon_y + 5, y_icon_x + 8, y_icon_y - 6, COLOR2D(0, 255, 0));
        graphics.DrawLine(y_icon_x - 3, y_icon_y + 6, y_icon_x + 8, y_icon_y - 5, COLOR2D(0, 255, 0));
        graphics.DrawLine(y_icon_x - 2, y_icon_y + 5, y_icon_x + 7, y_icon_y - 4, COLOR2D(0, 255, 0));
    }
}

void CDisplayManager::ShowAdvancedScreen(void)
{
    // Don't update if screen should be sleeping
    if (!ShouldAllowDisplayUpdates()) {
        return;
    }
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        // SH1106 implementation remains unchanged
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw title at the top
            m_pSH1106Display->DrawText(0, 2, "Advanced Menu", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font8x8);
            
            // Draw horizontal divider
            for (unsigned int x = 0; x < 128; x++)
            {
                m_pSH1106Display->SetPixel(x, 12, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // Draw only the Build Info option with selection indicator
            m_pSH1106Display->DrawText(10, 25, "Build Info", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Add selection arrows to indicate navigation
            m_pSH1106Display->DrawText(0, 25, ">", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Draw navigation instructions at bottom
            m_pSH1106Display->DrawText(0, 55, "KEY1: OK KEY2: Cancel", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
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
            
            // Clear the screen with WHITE background
            graphics.ClearScreen(COLOR2D(255, 255, 255));
            
            // Draw header bar with blue background
            graphics.DrawRect(0, 0, m_pST7789Display->GetWidth(), 30, COLOR2D(58, 124, 165));
            
            // Draw title text in white
            graphics.DrawText(10, 8, COLOR2D(255, 255, 255), "Advanced Menu", C2DGraphics::AlignLeft);
            
            // Draw only Build Information option (highlighted as selected)
            graphics.DrawRect(10, 40, m_pST7789Display->GetWidth() - 20, 40, COLOR2D(58, 124, 165));
            graphics.DrawText(20, 60, COLOR2D(255, 255, 255), "Build Information", C2DGraphics::AlignLeft);
            
            // Replace info icon with a small hammer icon for build info
            unsigned hammer_x = m_pST7789Display->GetWidth() - 40;
            unsigned hammer_y = 60;
            
            // Draw hammer head (claw hammer shape)
            // Main head body
            graphics.DrawRect(hammer_x - 10, hammer_y - 6, 12, 6, COLOR2D(255, 255, 255));
            
            // Claw part (back of hammer)
            graphics.DrawRect(hammer_x - 12, hammer_y - 5, 3, 2, COLOR2D(255, 255, 255));
            graphics.DrawRect(hammer_x - 13, hammer_y - 4, 2, 2, COLOR2D(255, 255, 255));
            
            // Strike face (front of hammer)
            graphics.DrawRect(hammer_x + 2, hammer_y - 5, 2, 4, COLOR2D(255, 255, 255));
            
            // Draw hammer handle
            graphics.DrawRect(hammer_x - 2, hammer_y, 3, 12, COLOR2D(255, 255, 255));
            
            // Add grip texture lines
            graphics.DrawLine(hammer_x - 1, hammer_y + 3, hammer_x, hammer_y + 3, COLOR2D(58, 124, 165));
            graphics.DrawLine(hammer_x - 1, hammer_y + 6, hammer_x, hammer_y + 6, COLOR2D(58, 124, 165));
            graphics.DrawLine(hammer_x - 1, hammer_y + 9, hammer_x, hammer_y + 9, COLOR2D(58, 124, 165));
            
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

void CDisplayManager::ShowBuildInfoScreen(const char* pVersionInfo, const char* pBuildDate, 
                                        const char* pGitBranch, const char* pGitCommit, const char* pBuildNumber)
{
    // Don't update if screen should be sleeping
    if (!ShouldAllowDisplayUpdates()) {
        return;
    }
    
    assert(pVersionInfo != nullptr);
    assert(pBuildDate != nullptr);
    assert(pGitBranch != nullptr);
    assert(pGitCommit != nullptr);
    assert(pBuildNumber != nullptr);
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw title at the top
            m_pSH1106Display->DrawText(0, 2, "Build Info", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font8x8);
            
            // Draw horizontal divider
            for (unsigned int x = 0; x < 128; x++)
            {
                m_pSH1106Display->SetPixel(x, 12, (CSH1106Display::TSH1106Color)SH1106_WHITE_COLOR);
            }
            
            // For SH1106, create a compact version of the build info
            char buildInfo[512];
            snprintf(buildInfo, sizeof(buildInfo), 
                     "%s\nBuild: %s%s\nBuild Date: %s\nBranch: %s%s\nCommit: %.8s", 
                     pVersionInfo, 
                     (strlen(pBuildNumber) > 0) ? pBuildNumber : "N/A",
                     (strlen(pBuildNumber) > 0) ? "" : "",
                     pBuildDate, pGitBranch,
                     (strcmp(pGitBranch, "main") == 0) ? " *" : "",
                     pGitCommit);
            
            // Display the compact build info with word wrapping
            const size_t chars_per_line = 21; // Maximum chars per line for SH1106
            
            size_t total_length = strlen(buildInfo);
            size_t current_pos = 0;
            unsigned int y_pos = 16; // Start position for text
            
            // Word wrapping implementation
            while (current_pos < total_length && y_pos < 55) {
                // Determine how many characters fit on this line
                size_t chars_to_display = chars_per_line;
                
                // Adjust for end of string
                if (current_pos + chars_to_display > total_length) {
                    chars_to_display = total_length - current_pos;
                } 
                // Try to break at word boundaries
                else if (current_pos + chars_to_display < total_length) {
                    // Find the last space in the line
                    size_t space_pos = chars_to_display;
                    while (space_pos > 0 && buildInfo[current_pos + space_pos] != ' ') {
                        space_pos--;
                    }
                    
                    // If we found a space, break there
                    if (space_pos > 0) {
                        chars_to_display = space_pos;
                    }
                }
                
                // Copy this line's text
                char line[32] = {0};
                strncpy(line, buildInfo + current_pos, chars_to_display);
                line[chars_to_display] = '\0';
                
                // Draw this line
                m_pSH1106Display->DrawText(0, y_pos, line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Move to next line
                current_pos += chars_to_display;
                
                // Skip space at beginning of next line
                if (current_pos < total_length && buildInfo[current_pos] == ' ') {
                    current_pos++;
                }
                
                y_pos += 10; // Line spacing
            }
            
            // Draw a "Back" instruction at the bottom
            m_pSH1106Display->DrawText(0, 56, "Press any key...", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            
            // Refresh the display
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
            
            // Clear the screen with WHITE background
            graphics.ClearScreen(COLOR2D(255, 255, 255));
            
            // Draw header bar with blue background
            graphics.DrawRect(0, 0, m_pST7789Display->GetWidth(), 30, COLOR2D(58, 124, 165));
            
            // Draw title text in white with a small hammer icon
            graphics.DrawText(40, 8, COLOR2D(255, 255, 255), "Build Info", C2DGraphics::AlignLeft);
            
            // Draw small hammer icon in the header
            unsigned hammer_x = 22;
            unsigned hammer_y = 15;
            
            // Draw hammer head (claw hammer shape)
            // Main head body
            graphics.DrawRect(hammer_x - 7, hammer_y - 4, 10, 6, COLOR2D(255, 255, 255));
            
            // Claw part (back of hammer)
            graphics.DrawRect(hammer_x - 9, hammer_y - 3, 3, 2, COLOR2D(255, 255, 255));
            graphics.DrawRect(hammer_x - 10, hammer_y - 2, 2, 2, COLOR2D(255, 255, 255));
            
            // Strike face (front of hammer)
            graphics.DrawRect(hammer_x + 3, hammer_y - 3, 2, 4, COLOR2D(255, 255, 255));
            
            // Draw hammer handle (wooden grip)
            graphics.DrawRect(hammer_x - 1, hammer_y + 2, 2, 8, COLOR2D(255, 255, 255));
            
            // Add grip texture lines
            graphics.DrawLine(hammer_x - 1, hammer_y + 4, hammer_x, hammer_y + 4, COLOR2D(58, 124, 165));
            graphics.DrawLine(hammer_x - 1, hammer_y + 6, hammer_x, hammer_y + 6, COLOR2D(58, 124, 165));
            graphics.DrawLine(hammer_x - 1, hammer_y + 8, hammer_x, hammer_y + 8, COLOR2D(58, 124, 165));
            
            // Draw content box with light blue background
            graphics.DrawRect(5, 40, m_pST7789Display->GetWidth() - 10, 160, COLOR2D(235, 245, 255));
            graphics.DrawRectOutline(5, 40, m_pST7789Display->GetWidth() - 10, 160, COLOR2D(58, 124, 165));
            
            // Improved layout with better space utilization
            const unsigned int line_spacing = 25;  // Slightly reduced spacing to fit more content
            const unsigned int left_margin = 15;
            unsigned int y_pos = 55;  // Start position for content
            
            // Line 1: Version info (clean version without git hash)
            char version_line[64];
            snprintf(version_line, sizeof(version_line), "Version: %s", pVersionInfo);
            graphics.DrawText(left_margin, y_pos, COLOR2D(0, 0, 140), version_line, C2DGraphics::AlignLeft);
            y_pos += line_spacing;
            
            // Line 2: Build number (if available)
            if (strlen(pBuildNumber) > 0) {
                char build_num_line[64];
                snprintf(build_num_line, sizeof(build_num_line), "Build: %s", pBuildNumber);
                graphics.DrawText(left_margin, y_pos, COLOR2D(0, 0, 140), build_num_line, C2DGraphics::AlignLeft);
                y_pos += line_spacing;
            }
            
            // Line 3: Build Date label
            graphics.DrawText(left_margin, y_pos, COLOR2D(0, 0, 140), "Build Date:", C2DGraphics::AlignLeft);
            y_pos += 20; // Smaller spacing for the date line
            
            // Line 4: Build Date value (on second line)
            graphics.DrawText(left_margin + 10, y_pos, COLOR2D(0, 0, 140), pBuildDate, C2DGraphics::AlignLeft);
            y_pos += line_spacing;
            
            // Line 5: Git branch (with star if main)
            char branch_line[64];
            if (strcmp(pGitBranch, "main") == 0) {
                snprintf(branch_line, sizeof(branch_line), "Branch: %s *", pGitBranch);
            } else {
                snprintf(branch_line, sizeof(branch_line), "Branch: %s", pGitBranch);
            }
            graphics.DrawText(left_margin, y_pos, COLOR2D(0, 0, 140), branch_line, C2DGraphics::AlignLeft);
            
            // Add git hash at the bottom of the content area (before navigation bar)
            char hash_line[64];
            char short_hash[9] = {0};  // 8 chars + null terminator
            strncpy(short_hash, pGitCommit, 8);
            snprintf(hash_line, sizeof(hash_line), "Commit: %s", short_hash);
            graphics.DrawText(left_margin, 175, COLOR2D(0, 0, 140), hash_line, C2DGraphics::AlignLeft);
            
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

void CDisplayManager::SetScreenPower(boolean bOn)
{
    // Minimal logging for power state changes
    m_pLogger->Write("dispman", LogNotice, "Screen power %s", bOn ? "ON" : "OFF");
    
    // Update screen state BEFORE changing hardware state
    m_bScreenActive = bOn;
    
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            if (bOn) {
                m_pSH1106Display->On();
            } else {
                m_pSH1106Display->Off();
            }
        }
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            if (bOn) {
                m_pST7789Display->On();
            } else {
                m_pST7789Display->Off();
            }
        }
        break;
        
    default:
        break;
    }
}

void CDisplayManager::DebugTimerAccuracy(void)
{
    static unsigned nStartTime = 0;
    static unsigned nLastCheckTime = 0;
    static unsigned nCheckCount = 0;
    
    unsigned nCurrentTime = CTimer::Get()->GetTicks();
    
    // Initialize start time if this is the first call
    if (nStartTime == 0) {
        nStartTime = nCurrentTime;
        nLastCheckTime = nCurrentTime;
        
        // Format current time using Circle's time functions
        CTime Time;
        CString TimeString;
        TimeString.Format("%02d:%02d:%02d", Time.GetHours(), Time.GetMinutes(), Time.GetSeconds());
        
        m_pLogger->Write("dispman", LogNotice, 
                      "[%s] Timer accuracy check started. Reference ticks=%u", 
                      (const char *)TimeString, nCurrentTime);
        return;
    }
    
    // Only log every second for first 10 checks, then less frequently
    unsigned nCheckInterval = (nCheckCount < 10) ? 1000 : 5000;
    
    if (nCurrentTime - nLastCheckTime > nCheckInterval) {
        CTime Time;
        CString TimeString;
        TimeString.Format("%02d:%02d:%02d", Time.GetHours(), Time.GetMinutes(), Time.GetSeconds());
        
        // Calculate elapsed time according to system timer
        unsigned nElapsedTicks = nCurrentTime - nStartTime;
        unsigned nIntervalTicks = nCurrentTime - nLastCheckTime;
        
        // Calculate elapsed seconds
        unsigned nElapsedSeconds = nElapsedTicks / 1000;
        
        m_pLogger->Write("dispman", LogNotice, 
                      "[%s] Timer check %u: elapsed=%u ticks (%u.%03u sec), interval=%u ticks", 
                      (const char *)TimeString,
                      ++nCheckCount,
                      nElapsedTicks,
                      nElapsedSeconds,
                      nElapsedTicks % 1000,
                      nIntervalTicks);
        
        nLastCheckTime = nCurrentTime;
    }
}

// Add this helper method to prevent unwanted screen wake-ups
boolean CDisplayManager::ShouldAllowDisplayUpdates(void)
{
    // If the screen is off due to timeout, block all display updates
    // that aren't explicitly initiated by WakeScreen()
    if (!m_bScreenActive && m_bMainScreenActive) {
        return FALSE;
    }
    
    return TRUE;
}

// In displaymanager.cpp - optimize SetMainScreenActive
void CDisplayManager::SetMainScreenActive(boolean bActive)
{
    // Only log if actually changing, with minimal details
    if (m_bMainScreenActive != bActive) {
        m_pLogger->Write("dispman", LogNotice, 
                      "Main screen %s", bActive ? "active" : "inactive");
    }
    
    // If we're entering the main screen, reset timer
    if (bActive && !m_bMainScreenActive) {
        WakeScreen(); // This resets timer and ensures screen is on
    }
    
    m_bMainScreenActive = bActive;
}

// Add these implementations to displaymanager.cpp

void CDisplayManager::WakeScreen(void)
{
    // Reset the last activity time
    m_nLastActivityTime = CTimer::Get()->GetTicks();
    
    // If screen is not active, turn it on
    if (!m_bScreenActive) {
        // Only log when actually waking up
        m_pLogger->Write("dispman", LogNotice, "Screen woken up");
        
        SetScreenPower(TRUE);
        m_bScreenActive = TRUE;
        m_bTimeoutWarningShown = FALSE;
    }
}

void CDisplayManager::UpdateScreenTimeout(void)
{
    // Don't timeout if not on main screen or already sleeping
    if (!m_bMainScreenActive || !m_bScreenActive) {
        return;
    }
    
    // Get current time
    unsigned nCurrentTime = CTimer::Get()->GetTicks();
    
    // Calculate elapsed time in seconds
    unsigned nElapsedSeconds = (nCurrentTime - m_nLastActivityTime) / 1000;
    
    // Check for actual timeout first (this ensures we don't get stuck in warning state)
    if (nElapsedSeconds >= m_nScreenTimeoutSeconds) {
        // Only log and act if we're actually changing state
        if (m_bScreenActive) {
            // Keep this important state change log
            m_pLogger->Write("dispman", LogNotice, 
                          "Screen sleeping: elapsed=%u sec, timeout=%u sec", 
                          nElapsedSeconds, m_nScreenTimeoutSeconds);
            
            // Set state to FALSE before turning off to prevent race conditions
            m_bScreenActive = FALSE; 
            SetScreenPower(FALSE);
        }
        return; // Exit early to avoid warning check
    }
    
    // Check if we need to show warning (2 seconds before timeout)
    if (!m_bTimeoutWarningShown && nElapsedSeconds >= m_nScreenTimeoutSeconds - 2) {
        // Keep warning log
        m_pLogger->Write("dispman", LogNotice, 
                      "Showing sleep warning: elapsed=%u sec, timeout=%u sec", 
                      nElapsedSeconds, m_nScreenTimeoutSeconds);
        
        ShowTimeoutWarning();
        m_bTimeoutWarningShown = TRUE;
    }
}

// Also need to add the ShowTimeoutWarning implementation which was called but missing
void CDisplayManager::ShowTimeoutWarning(void)
{
    // For now, just show a simple timeout warning on each display
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Draw a simple warning at the bottom of the screen
            m_pSH1106Display->DrawText(5, 55, "Sleep in 2s...", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                                     FALSE, FALSE, Font6x7);
            m_pSH1106Display->Refresh();
        }
        break;
        
    case DisplayTypeST7789:
        if (m_pST7789Display != nullptr)
        {
            // For ST7789, create a translucent warning bar at the bottom
            C2DGraphics graphics(m_pST7789Display);
            if (graphics.Initialize())
            {
                // Draw a semi-transparent warning bar
                graphics.DrawRect(0, 190, m_pST7789Display->GetWidth(), 20, COLOR2D(40, 40, 40));
                graphics.DrawText(m_pST7789Display->GetWidth()/2, 200, COLOR2D(255, 255, 255), 
                                "Screen will sleep in 2s...", C2DGraphics::AlignCenter);
                graphics.UpdateDisplay();
            }
        }
        break;
        
    default:
        break;
    }
}