//
// gpiobuttonmanager.cpp
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
#include "gpiobuttonmanager.h"
#include <usbode-display/sh1106device.h>
#include <usbode-display/st7789display.h>
#include <assert.h>

LOGMODULE ("gpiobutton");

static const char FromGPIOButtonManager[] = "buttons";

CGPIOButtonManager::CGPIOButtonManager(CLogger *pLogger, TDisplayType DisplayType)
    : m_pLogger(pLogger),
      m_DisplayType(DisplayType),
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
    assert(m_pLogger != nullptr);
    
    // Button configuration depends on display type
    switch (DisplayType)
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

CGPIOButtonManager::~CGPIOButtonManager(void)
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
}

boolean CGPIOButtonManager::Initialize(void)
{
    // Early exit if no buttons to initialize
    if (m_nButtonCount == 0)
    {
        LOGNOTE("No buttons to initialize for this display type");
        return TRUE;
    }
    
    LOGNOTE("Initializing %u buttons for %s display", 
            m_nButtonCount,
            m_DisplayType == DisplayTypeSH1106 ? "SH1106" : 
            m_DisplayType == DisplayTypeST7789 ? "ST7789" : "Unknown");
    
    // Allocate arrays for button states and pins
    m_ppButtonPins = new CGPIOPin*[m_nButtonCount];
    m_pButtonStates = new boolean[m_nButtonCount];
    m_pLastPressTime = new unsigned[m_nButtonCount];
    m_pLastReportedState = new boolean[m_nButtonCount];
    
    if (m_ppButtonPins == nullptr || m_pButtonStates == nullptr || 
        m_pLastPressTime == nullptr || m_pLastReportedState == nullptr)
    {
        LOGERR("Failed to allocate button arrays");
        return FALSE;
    }
    
    // Initialize GPIO pins for each button
    for (unsigned i = 0; i < m_nButtonCount; i++)
    {
        LOGDBG("Initializing button %u (%s) on GPIO %u",
               i, m_pButtonLabels[i], m_pButtonPins[i]);
                        
        m_ppButtonPins[i] = new CGPIOPin(m_pButtonPins[i], GPIOModeInputPullUp);
        
        if (m_ppButtonPins[i] == nullptr)
        {
            LOGERR("Failed to initialize button %u", i);
            return FALSE;
        }
        
        // Initialize button states
        m_pButtonStates[i] = FALSE;
        m_pLastPressTime[i] = 0;
        m_pLastReportedState[i] = FALSE;
    }
    
    // Short delay for pins to stabilize
    CTimer::Get()->MsDelay(20);
    
    // Log the configured pins
    LOGNOTE("=== Button Configuration ===");
    for (unsigned i = 0; i < m_nButtonCount; i++)
    {
        LOGNOTE("Button %u: %s (GPIO%u)", i, m_pButtonLabels[i], m_pButtonPins[i]);
    }
    LOGNOTE("=== End Button Configuration ===");
    
    LOGNOTE("Button initialization complete");
    
    return TRUE;
}

void CGPIOButtonManager::RegisterEventHandler(TButtonEventHandler pHandler, void* pParam)
{
    m_pEventHandler = pHandler;
    m_pCallbackParam = pParam;
    
    LOGNOTE("Button event handler registered");
}

boolean CGPIOButtonManager::IsButtonPressed(unsigned nButtonIndex)
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

const char* CGPIOButtonManager::GetButtonLabel(unsigned nButtonIndex) const
{
    if (nButtonIndex >= m_nButtonCount || m_pButtonLabels == nullptr)
    {
        return "Unknown";
    }
    
    return m_pButtonLabels[nButtonIndex];
}

void CGPIOButtonManager::Update(void)
{
    // Process all buttons
    for (unsigned i = 0; i < m_nButtonCount; i++)
    {
        if (m_ppButtonPins[i] != nullptr)
        {
            // Read the raw button state (buttons are active LOW with pull-up)
            boolean bCurrentState = (m_ppButtonPins[i]->Read() == LOW);
            
            // Process the button state using the existing method
            ProcessButtonState(i, bCurrentState);
        }
    }
}

void CGPIOButtonManager::ProcessButtonState(unsigned nButtonIndex, boolean bCurrentState)
{
    unsigned nTicks = CTimer::Get()->GetTicks();
    
    // Check if the state has changed
    if (bCurrentState != m_pLastReportedState[nButtonIndex])
    {
        // If enough time has passed since the last state change (debouncing)
        if (nTicks - m_pLastPressTime[nButtonIndex] > DEBOUNCE_TIME_MS)
        {
            // Update the last press time immediately
            m_pLastPressTime[nButtonIndex] = nTicks;
            
            // Update the button state safely
            m_Lock.Acquire();
            m_pButtonStates[nButtonIndex] = bCurrentState;
            m_Lock.Release();
            
            // Update the last reported state immediately
            m_pLastReportedState[nButtonIndex] = bCurrentState;
            
            // IMPORTANT: Call the event handler IMMEDIATELY for press events
            // This is key to responsive UI - handle button presses right away
            if (m_pEventHandler != nullptr)
            {
                (*m_pEventHandler)(nButtonIndex, bCurrentState, m_pCallbackParam);
            }
            
            // Log state changes (reduce logging for faster response)
            if (bCurrentState)
            {
                LOGNOTE("Button %s (%u) PRESSED", GetButtonLabel(nButtonIndex), nButtonIndex);
            }
        }
    }
}

void CGPIOButtonManager::InitSH1106Buttons(void)
{
    // Use button configuration from SH1106Device
    m_nButtonCount = CSH1106Device::NUM_GPIO_BUTTONS;
    m_pButtonPins = const_cast<unsigned*>(CSH1106Device::GPIO_BUTTON_PINS);
    m_pButtonLabels = const_cast<const char**>(CSH1106Device::GPIO_BUTTON_LABELS);
}

void CGPIOButtonManager::InitST7789Buttons(void)
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

void CGPIOButtonManager::DebugPrintPinStates(void)
{
    LOGNOTE("=== Button States ===");
    for (unsigned i = 0; i < m_nButtonCount; i++)
    {
        if (m_ppButtonPins[i] != nullptr)
        {
            // Read raw pin state
            boolean bRawState = (m_ppButtonPins[i]->Read() == LOW);
            
            LOGNOTE("Button %u (%s) - GPIO%u: Raw=%s, Debounced=%s", 
                   i, m_pButtonLabels[i], m_pButtonPins[i],
                   bRawState ? "PRESSED" : "released", 
                   m_pButtonStates[i] ? "PRESSED" : "released");
        }
    }
    LOGNOTE("=== End Button States ===");
}