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
#include <circle/2dgraphics.h>
#include <circle/timer.h>
#include "sh1106display.h"
#include "sh1106device.h"
#include "st7789display.h"
#include "st7789device.h"

enum TDisplayType
{
    DisplayTypeSH1106,     // SH1106 OLED display
    DisplayTypeST7789,     // ST7789 TFT display
    DisplayTypeUnknown     // Unknown or unspecified display
};

class CDisplayManager
{
public:
    // Constructor now takes screen timeout parameter
    CDisplayManager(CLogger *pLogger, TDisplayType DisplayType, unsigned nScreenTimeoutSeconds = 5);
    
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
    void ShowStatusScreen(const char *pTitle, const char *pIPAddress, const char *pISOName, const char *pUSBSpeed);
    void ShowFileSelectionScreen(const char *pCurrentISOName, const char *pSelectedFileName, 
                             unsigned int CurrentFileIndex, unsigned int TotalFiles);
    void ShowButtonTestScreen(void); // Added method for button test screen
    void Refresh(void); // Added method declaration for Refresh
    void ShowButtonPress(unsigned nButtonIndex, const char* pButtonLabel); // Added method declaration for button press display
    void ShowAdvancedScreen(void); // Added method declaration for advanced screen
    void ShowBuildInfoScreen(const char* pVersionInfo, const char* pBuildDate, 
                            const char* pGitBranch, const char* pGitCommit); // Method for build info
    
    // Screen timeout methods
    void WakeScreen(void); // Wake up the screen from sleep
    void UpdateScreenTimeout(void); // Call this periodically to handle screen timeout
    boolean IsMainScreenActive(void) const { return m_bMainScreenActive; }
    void SetMainScreenActive(boolean bActive); // Set main screen active status
    void SetScreenTimeout(unsigned nSeconds); // Change the screen timeout value
    void DebugTimerAccuracy(void); // For debugging timer accuracy
    boolean ShouldAllowDisplayUpdates(void);

private:
    // Initialize SH1106 display
    boolean InitializeSH1106(CSPIMaster *pSPIMaster);
    
    // Initialize ST7789 display
    boolean InitializeST7789(CSPIMaster *pSPIMaster);
    
    void DrawNavigationBar(C2DGraphics& graphics, const char* screenType);
    
    // Helper methods for screen timeout
    void ShowTimeoutWarning(void); // Show warning before sleep
    void SetScreenPower(boolean bOn); // Turn screen on/off

    CLogger *m_pLogger;
    TDisplayType m_DisplayType;
    
    // SH1106 display components
    CSH1106Display *m_pSH1106Display;
    CSH1106Device *m_pSH1106Device;
    
    // ST7789 display components
    CST7789Display *m_pST7789Display;
    CST7789Device *m_pST7789Device;
    
    // Screen timeout variables
    unsigned m_nScreenTimeoutSeconds;
    unsigned m_nLastActivityTime;
    boolean m_bScreenActive; // Whether screen is awake
    boolean m_bTimeoutWarningShown; // Whether warning has been shown
    boolean m_bMainScreenActive; // Whether we're on the main screen
};

#endif
