//
// gpiobuttonmanager.h
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
#ifndef _gpiobuttonmanager_h
#define _gpiobuttonmanager_h

#include <circle/logger.h>
#include <circle/types.h>
#include <circle/gpiopin.h>
#include <circle/timer.h>
#include <circle/spinlock.h>
#include "displaymanager.h"

// Button event handler callback type
typedef void (*TButtonEventHandler)(unsigned nButtonIndex, boolean bPressed, void* pParam);

class CGPIOButtonManager
{
public:
    // Constructor that takes display type
    CGPIOButtonManager(CLogger *pLogger, TDisplayType DisplayType);
    
    // Destructor
    ~CGPIOButtonManager(void);
    
    // Initialization - no task creation here, just configuration
    boolean Initialize(void);
    
    // Get display type
    TDisplayType GetDisplayType(void) const { return m_DisplayType; }
    
    // Register a callback for button events
    void RegisterEventHandler(TButtonEventHandler pHandler, void* pParam = nullptr);
    
    // Get current button state
    boolean IsButtonPressed(unsigned nButtonIndex);
    
    // Returns the number of buttons for the current display type
    unsigned GetButtonCount(void) const { return m_nButtonCount; }
    
    // Get button label/name for the given button index
    const char* GetButtonLabel(unsigned nButtonIndex) const;
    
    // Update method to be called regularly from the main loop
    void Update(void);
    
private:
    // Button debouncing and state management
    void ProcessButtonState(unsigned nButtonIndex, boolean bCurrentState);
    
    // Initialization helpers
    void InitSH1106Buttons(void);
    void InitST7789Buttons(void);
    
    // Debug helper to print current pin states
    void DebugPrintPinStates(void);
    
private:
    CLogger *m_pLogger;
    TDisplayType m_DisplayType;
    
    // Button configuration
    unsigned m_nButtonCount;
    unsigned* m_pButtonPins;
    const char** m_pButtonLabels;
    
    // GPIO pin objects for each button
    CGPIOPin** m_ppButtonPins;
    
    // Button states
    boolean* m_pButtonStates;
    
    // Debounce variables
    unsigned* m_pLastPressTime;
    boolean* m_pLastReportedState;
    
    // Thread synchronization
    CSpinLock m_Lock;
    
    // Callback
    TButtonEventHandler m_pEventHandler;
    void* m_pCallbackParam;
    
    // Constants
    static const unsigned DEBOUNCE_TIME_MS = 150;  // Increased from 50ms to 150ms
};

#endif