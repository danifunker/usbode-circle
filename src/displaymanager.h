//
// displaymanager.h
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
#ifndef _display_displaymanager_h
#define _display_displaymanager_h

#include <circle/spimaster.h>
#include <circle/device.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <circle/gpiopin.h>
#include <usbode-display/sh1106display.h>
#include <usbode-display/sh1106device.h>
#include <usbode-display/st7789display.h>
#include <usbode-display/st7789device.h>

enum TDisplayType
{
    DisplayTypeSH1106,     // SH1106 OLED display
    DisplayTypeST7789,     // ST7789 TFT display
    DisplayTypeUnknown     // Unknown or unspecified display
};

class CDisplayManager
{
public:
    // Modified constructor that takes display type
    CDisplayManager(CLogger *pLogger, TDisplayType DisplayType);
    
    // Destructor
    ~CDisplayManager(void);
    
    // Initialization
    boolean Initialize(CSPIMaster *pSPIMaster);
    
    // Get display type
    TDisplayType GetDisplayType(void) const { return m_DisplayType; }
    
    // Get display device
    CDevice *GetDisplayDevice(void) const;
    
    // Get display
    CDisplay *GetDisplay(void) const;
    
    // Utility methods
    void ClearDisplay(void);
    void ShowStatusScreen(const char *pTitle, const char *pIPAddress, const char *pISOName);
    void ShowFileSelectionScreen(const char *pCurrentISOName, const char *pSelectedFileName, 
                             unsigned int CurrentFileIndex, unsigned int TotalFiles);
    
private:
    // Initialize SH1106 display
    boolean InitializeSH1106(CSPIMaster *pSPIMaster);
    
    // Initialize ST7789 display
    boolean InitializeST7789(CSPIMaster *pSPIMaster);
    
private:
    CLogger *m_pLogger;
    TDisplayType m_DisplayType;
    
    // SH1106 display components
    CSH1106Display *m_pSH1106Display;
    CSH1106Device *m_pSH1106Device;
    
    // ST7789 display components
    CST7789Display *m_pST7789Display;
    CST7789Device *m_pST7789Device;
};

#endif