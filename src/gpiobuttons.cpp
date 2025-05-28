//
// gpiobuttons.cpp
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
#include "gpiobuttons.h"
#include <usbode-display/sh1106device.h>
#include <usbode-display/st7789display.h>
#include <assert.h>

#define LOGNAME "GPIOButtons"

// Task stack size - adjust based on your needs
#define TASK_STACK_SIZE_BUTTONS 4096

// Update the constructor to create the task in suspended state
CGPIOButtons::CGPIOButtons(TDisplayType displayType, CLogger* pLogger)
    : CTask(TASK_STACK_SIZE_BUTTONS, TRUE),  // TRUE = create suspended
      m_DisplayType(displayType),
      m_pLogger(pLogger),
      m_nButtonCount(0),
      m_pButtonPins(nullptr),
      m_pButtonLabels(nullptr),
      m_ppButtonPins(nullptr),
      m_pButtonStates(nullptr),
      m_pLastPressTime(nullptr),
      m_pLastReportedState(nullptr),
      m_pEventHandler(nullptr),
      m_pCallbackParam(nullptr)
{
    // Set the task name
    SetName(LOGNAME);
    
    // Button configuration depends on display type
    switch (displayType)
    {
        case DisplayTypeSH1106:
            InitSH1106Buttons();
            break;
            
        case DisplayTypeST7789:
            InitST7789Buttons();
            break;
            
        default:
            // No buttons for unknown display
            m_nButtonCount = 0;
            break;
    }
}

CGPIOButtons::~CGPIOButtons(void)
{
    // Clean up resources
    if (m_ppButtonPins != nullptr)
    {
        for (unsigned i = 0; i < m_nButtonCount; i++)
        {
            if (m_ppButtonPins[i] != nullptr)
            {
                delete m_ppButtonPins[i];
            }
        }
        delete[] m_ppButtonPins;
    }
    
    delete[] m_pButtonStates;
    delete[] m_pLastPressTime;
    delete[] m_pLastReportedState;
    
    // m_pButtonPins and m_pButtonLabels are not owned by this class
    // and should not be deleted here
}

boolean CGPIOButtons::Initialize(void)
{
    // Early exit if no buttons to initialize
    if (m_nButtonCount == 0)
    {
        return TRUE;
    }
    
    // Allocate arrays for button states and pins
    m_ppButtonPins = new CGPIOPin*[m_nButtonCount];
    m_pButtonStates = new boolean[m_nButtonCount];
    m_pLastPressTime = new unsigned[m_nButtonCount];
    m_pLastReportedState = new boolean[m_nButtonCount];
    
    if (m_ppButtonPins == nullptr || m_pButtonStates == nullptr || 
        m_pLastPressTime == nullptr || m_pLastReportedState == nullptr)
    {
        return FALSE;
    }
    
    // Initialize GPIO pins for each button
    for (unsigned i = 0; i < m_nButtonCount; i++)
    {
        m_ppButtonPins[i] = new CGPIOPin(m_pButtonPins[i], GPIOModeInputPullUp);
        
        if (m_ppButtonPins[i] == nullptr)
        {
            return FALSE;
        }
        
        // Initialize button states
        m_pButtonStates[i] = FALSE;
        m_pLastPressTime[i] = 0;
        m_pLastReportedState[i] = FALSE;
    }
    
    // Short delay for pins to stabilize
    CTimer::Get()->MsDelay(20);
    
    // Start the task after initialization
    CTask::Start();
    
    return TRUE;
}

void CGPIOButtons::RegisterEventHandler(TButtonEventHandler pHandler, void* pParam)
{
    m_pEventHandler = pHandler;
    m_pCallbackParam = pParam;
}

boolean CGPIOButtons::IsButtonPressed(unsigned nButtonIndex)
{
    if (nButtonIndex >= m_nButtonCount)
    {
        return FALSE;
    }
    
    // Thread-safe access to button state
    m_Lock.Acquire();
    boolean bState = m_pButtonStates[nButtonIndex];
    m_Lock.Release();
    
    return bState;
}

const char* CGPIOButtons::GetButtonLabel(unsigned nButtonIndex) const
{
    if (nButtonIndex >= m_nButtonCount || m_pButtonLabels == nullptr)
    {
        return "Unknown";
    }
    
    return m_pButtonLabels[nButtonIndex];
}

void CGPIOButtons::Run(void)
{
    if (m_pLogger)
    {
        m_pLogger->Write(LOGNAME, LogNotice, "Button monitoring task started");
    }
    
    // Main button polling loop
    while (1)
    {
        // Check each button
        for (unsigned i = 0; i < m_nButtonCount; i++)
        {
            if (m_ppButtonPins[i] != nullptr)
            {
                // Buttons are active LOW (pressed when reading LOW)
                boolean bCurrentState = (m_ppButtonPins[i]->Read() == LOW);
                
                // Process button state with debouncing
                ProcessButtonState(i, bCurrentState);
            }
        }
        
        // Sleep before polling again - use CTask methods for sleeping
        CTimer::Get()->MsDelay(POLL_INTERVAL_MS);
    }
}

void CGPIOButtons::ProcessButtonState(unsigned nButtonIndex, boolean bCurrentState)
{
    unsigned nTicks = CTimer::Get()->GetTicks();
    
    // Check if the state has changed
    if (bCurrentState != m_pLastReportedState[nButtonIndex])
    {
        // If enough time has passed since the last state change (debouncing)
        if (nTicks - m_pLastPressTime[nButtonIndex] > DEBOUNCE_TIME_MS)
        {
            // Update the last press time
            m_pLastPressTime[nButtonIndex] = nTicks;
            
            // Update the button state safely
            m_Lock.Acquire();
            m_pButtonStates[nButtonIndex] = bCurrentState;
            m_Lock.Release();
            
            // Update the last reported state
            m_pLastReportedState[nButtonIndex] = bCurrentState;
            
            // Call the event handler if registered
            if (m_pEventHandler != nullptr)
            {
                (*m_pEventHandler)(nButtonIndex, bCurrentState, m_pCallbackParam);
            }
            
            // Log button state change if logger is available
            if (m_pLogger)
            {
                m_pLogger->Write(LOGNAME, LogDebug, "Button %s (%u) %s", 
                                 GetButtonLabel(nButtonIndex), nButtonIndex,
                                 bCurrentState ? "pressed" : "released");
            }
        }
    }
}

void CGPIOButtons::InitSH1106Buttons(void)
{
    // Use button configuration from SH1106Device
    m_nButtonCount = CSH1106Device::NUM_GPIO_BUTTONS;
    m_pButtonPins = const_cast<unsigned*>(CSH1106Device::GPIO_BUTTON_PINS);
    m_pButtonLabels = const_cast<const char**>(CSH1106Device::GPIO_BUTTON_LABELS);
}

void CGPIOButtons::InitST7789Buttons(void)
{
    // Use button configuration from ST7789Display
    static const unsigned ST7789_NUM_BUTTONS = 4;
    static const unsigned ST7789_BUTTON_PINS[ST7789_NUM_BUTTONS] = {
        CST7789Display::BUTTON_A_PIN,
        CST7789Display::BUTTON_B_PIN,
        CST7789Display::BUTTON_X_PIN,
        CST7789Display::BUTTON_Y_PIN
    };
    
    static const char* ST7789_BUTTON_LABELS[ST7789_NUM_BUTTONS] = {
        "A (Up)", "B (Down)", "X (Cancel)", "Y (Select)"
    };
    
    m_nButtonCount = ST7789_NUM_BUTTONS;
    m_pButtonPins = const_cast<unsigned*>(ST7789_BUTTON_PINS);
    m_pButtonLabels = const_cast<const char**>(ST7789_BUTTON_LABELS);
}