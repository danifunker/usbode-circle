// filepath: /Users/daniusa/repos/circle/addon/usbode-display/sh1106device.cpp
//
// sh1106device.cpp
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
#include "sh1106device.h"
#include <circle/timer.h>
#include <circle/chargenerator.h>
#include <assert.h>

const unsigned CSH1106Device::GPIO_BUTTON_PINS[NUM_GPIO_BUTTONS] = {
    BUTTON_UP_PIN, BUTTON_CENTER_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN,
    BUTTON_MID_PIN, BUTTON_KEY1_PIN, BUTTON_KEY2_PIN, BUTTON_KEY3_PIN
};

// Initialize the static button labels array
const char* CSH1106Device::GPIO_BUTTON_LABELS[NUM_GPIO_BUTTONS] = {
    "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT", "CENTER", "KEY1", "KEY2", "KEY3"
};

CSH1106Device::CSH1106Device (CSPIMaster *pSPIMaster, CSH1106Display *pSH1106Display,
                           unsigned nColumns, unsigned nRows,
                           const TFont &rFont, bool bDoubleWidth, bool bDoubleHeight,
                           boolean bBlockCursor)
:   CCharDevice (nColumns, nRows),
    m_pSH1106Display (pSH1106Display),
    m_nColumns (nColumns),
    m_nRows (nRows),
    m_rFont (rFont),
    m_bDoubleWidth (bDoubleWidth),
    m_bDoubleHeight (bDoubleHeight)
{
}

CSH1106Device::~CSH1106Device (void)
{
}

boolean CSH1106Device::Initialize (void)
{
    // SH1106 display assumed to already be initialized prior to
    // initializing the character device, so nothing more to be
    // done here other than checking the dimensions are sensible...
    
    unsigned w = m_pSH1106Display->GetWidth();
    unsigned h = m_pSH1106Display->GetHeight();
    
    // sh1106display uses the chargenerator, so check some properties here.
    CCharGenerator cg (m_rFont, CCharGenerator::MakeFlags (m_bDoubleWidth, m_bDoubleHeight));
    m_nCharW = cg.GetCharWidth();
    m_nCharH = cg.GetCharHeight();

    if (m_nColumns * m_nCharW > w)
    {
        // Limit number of columns to width of screen
        m_nColumns = w / m_nCharW;
    }
    if (m_nRows * m_nCharH > h)
    {
        // Limit number of rows to height of screen
        m_nRows = h / m_nCharH;
    }
    
    // Clear screen to black and turn on display
    m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
    m_pSH1106Display->On();

    return CCharDevice::Initialize();
}

void CSH1106Device::DevClearCursor (void)
{
    // Just clear the display
    m_pSH1106Display->Clear(SH1106_BLACK_COLOR);
}

void CSH1106Device::DevSetCursorMode (boolean bVisible)
{
    // SH1106 doesn't support hardware cursor
}

void CSH1106Device::DevSetChar (unsigned nPosX, unsigned nPosY, char chChar)
{
    char s[2];
    s[0] = chChar;
    s[1] = '\0';

    if ((nPosX >= m_nColumns) || (nPosY >= m_nRows))
    {
        // Off the display so quit
        return;
    }

    // Convert from cursor coordinates to pixel coordinates
    unsigned nXC = nPosX * m_nCharW;
    unsigned nYC = nPosY * m_nCharH;

    m_pSH1106Display->DrawText(nXC, nYC, s, SH1106_WHITE_COLOR, SH1106_BLACK_COLOR,
                             m_bDoubleWidth, m_bDoubleHeight, m_rFont);
}

void CSH1106Device::DevSetCursor (unsigned nCursorX, unsigned nCursorY)
{
    // SH1106 doesn't support hardware cursor
}

void CSH1106Device::DevUpdateDisplay (void)
{
    // This method is called by CCharDevice when the display needs updating
    if (m_pSH1106Display != nullptr)
    {
        // Use the public Refresh method instead of trying to access the private UpdateDisplay method
        m_pSH1106Display->Refresh();
    }
}

bool CSH1106Device::IsButtonPressed(unsigned nButtonIndex)
{
    static CGPIOPin* buttonPins[NUM_GPIO_BUTTONS] = { nullptr };
    
    // Lazy initialization of GPIO pins
    if (buttonPins[nButtonIndex] == nullptr) {
        if (nButtonIndex < NUM_GPIO_BUTTONS) {
            buttonPins[nButtonIndex] = new CGPIOPin(GPIO_BUTTON_PINS[nButtonIndex], GPIOModeInputPullUp);
            CTimer::Get()->MsDelay(10); // Short delay for pin to stabilize
        } else {
            return false;
        }
    }
    
    // Return true if button is pressed (active low)
    return buttonPins[nButtonIndex]->Read() == LOW;
}