//
// gpiobuttons.h
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
#ifndef _display_gpiobuttons_h
#define _display_gpiobuttons_h

#include <circle/gpiopin.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/logger.h>
#include <circle/spinlock.h>
#include "displaymanager.h"

// Button event handler callback type
typedef void (*TButtonEventHandler)(unsigned nButtonIndex, boolean bPressed, void* pParam);

class CGPIOButtons : public CTask
{
public:
    // Constructor for different display types
    CGPIOButtons(TDisplayType displayType, CLogger* pLogger);
    ~CGPIOButtons(void);

    // Initialize the button handler
    boolean Initialize(void);
    
    // Start monitoring buttons in a separate thread
    boolean Start(void);
    
    // Register a callback for button events
    void RegisterEventHandler(TButtonEventHandler pHandler, void* pParam = nullptr);
    
    // Get current button state (thread-safe)
    boolean IsButtonPressed(unsigned nButtonIndex);
    
    // Returns the number of buttons for the current display type
    unsigned GetButtonCount(void) const { return m_nButtonCount; }
    
    // Get button label/name for the given button index
    const char* GetButtonLabel(unsigned nButtonIndex) const;

private:
    // Thread entry point
    void Run(void) override;
    
    // Button debouncing and state management
    void ProcessButtonState(unsigned nButtonIndex, boolean bCurrentState);
    
    // Initialization helpers
    void InitSH1106Buttons(void);
    void InitST7789Buttons(void);

private:
    TDisplayType m_DisplayType;
    CLogger* m_pLogger;
    
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
    static const unsigned DEBOUNCE_TIME_MS = 50;
    static const unsigned POLL_INTERVAL_MS = 10;
};

#endif