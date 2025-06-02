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
    // TODO: Implement ST7789 initialization when driver is available
    m_pLogger->Write(FromDisplayManager, LogWarning, "ST7789 support not yet implemented");
    
    return FALSE;
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

CDisplay *CDisplayManager::GetDisplay(void) const
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        return m_pSH1106Display;
        
    case DisplayTypeST7789:
        // return m_pST7789Display;
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
        // Clear ST7789 display when implemented
        break;
        
    default:
        break;
    }
}

void CDisplayManager::ShowStatusScreen(const char *pTitle, const char *pIPAddress, const char *pISOName)
{
    assert(pTitle != nullptr);
    assert(pIPAddress != nullptr);
    assert(pISOName != nullptr);
    
    // Get USB speed information
    boolean bUSBFullSpeed = CKernelOptions::Get()->GetUSBFullSpeed();
    const char* pUSBSpeed = bUSBFullSpeed ? "USB1.1" : "USB2.0";
    
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
        // Show status screen on ST7789 when implemented
        break;
        
    default:
        break;
    }
}

void CDisplayManager::ShowFileSelectionScreen(const char *pCurrentISOName, const char *pSelectedFileName, 
                                           unsigned int CurrentFileIndex, unsigned int TotalFiles)
{
    switch (m_DisplayType)
    {
    case DisplayTypeSH1106:
        if (m_pSH1106Display != nullptr)
        {
            // Clear display first
            m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
            
            // Draw title at the top - moved down slightly
            m_pSH1106Display->DrawText(0, 2, "Select an ISO:", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                     FALSE, FALSE, Font6x7);
            
            // Use the current ISO passed as parameter
            const char* currentImage = pCurrentISOName;
            
            // First line has "I: " prefix, so fewer characters per line
            const size_t first_line_chars = 18;
            // Second line and selection lines have no prefix, so can use more characters
            const size_t chars_per_line = 21;
            
            // Calculate line y position based on current ISO length
            unsigned int line_y;
            
            // CURRENT ISO (Top of screen) =========================
            if (strlen(currentImage) <= first_line_chars)
            {
                // Short name fits on one line
                m_pSH1106Display->DrawText(0, 12, "I: ", SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                m_pSH1106Display->DrawText(12, 12, currentImage, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                line_y = 22;
            }
            else
            {
                // First line with prefix
                char first_line[32];
                snprintf(first_line, sizeof(first_line), "I: %.18s", currentImage);
                m_pSH1106Display->DrawText(0, 12, first_line, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR, 
                                         FALSE, FALSE, Font6x7);
                
                // Second line handling for very long names
                char second_line[32] = {0};
                
                if (strlen(currentImage) > first_line_chars + chars_per_line - 12)
                {
                    // Very long name, ensure last 11 chars are visible
                    size_t remaining_chars = chars_per_line - 12;  // Space for "…" and last 11 chars
                    
                    // Copy first part
                    strncpy(second_line, currentImage + first_line_chars, remaining_chars);
                    second_line[remaining_chars] = '\0';
                    
                    // Add ellipsis and last 11 chars
                    strcat(second_line, "…");
                    strcat(second_line, currentImage + strlen(currentImage) - 11);
                }
                else
                {
                    // Just copy the remaining part
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
            
            // POSITION INDICATOR =========================
            // Display file position indicator only ONCE at bottom (moved up)
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
        // Implement ST7789 version when needed
        break;
        
    default:
        break;
    }
}