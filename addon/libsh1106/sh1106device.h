//
// sh1106device.h
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
#ifndef _display_sh1106device_h
#define _display_sh1106device_h

#include <circle/device.h>
#include <circle/gpiopin.h>
#include <circle/spinlock.h>
#include <circle/types.h>
#include <display/chardevice.h>
#include "sh1106display.h"

class CSH1106Device : public CCharDevice   /// OLED display driver (using SH1106 controller)
{
public:
    /// \param pSPIMaster
    /// \param pSH1106Display
    /// \param nColumns Display size in number of columns
    /// \param nRows    Display size in number of rows
    /// \param rFont    Font to be used
    /// \param bDoubleWidth Use thicker characters on screen
    /// \param bDoubleHeight Use higher characters on screen
    /// \param bBlockCursor Use blinking block cursor instead of underline cursor
    CSH1106Device (CSPIMaster *pSPIMaster, CSH1106Display *pSH1106Display,
                 unsigned nColumns, unsigned nRows,
                 const TFont &rFont = Font8x8,
                 bool bDoubleWidth = FALSE, bool bDoubleHeight = FALSE,
                 boolean bBlockCursor = FALSE);

    ~CSH1106Device (void);

    /// \return Operation successful?
    boolean Initialize (void);

private:
    void DevClearCursor (void) override;
    void DevSetCursor (unsigned nCursorX, unsigned nCursorY) override;
    void DevSetCursorMode (boolean bVisible) override;
    void DevSetChar (unsigned nPosX, unsigned nPosY, char chChar) override;
    void DevUpdateDisplay (void) override;

public:
    // GPIO Button Configuration - for applications using the display
    static const unsigned NUM_GPIO_BUTTONS = 8;
    
    // Button GPIO pin numbers
    static const unsigned BUTTON_UP_PIN = 6;
    static const unsigned BUTTON_CENTER_PIN = 19;
    static const unsigned BUTTON_LEFT_PIN = 5;
    static const unsigned BUTTON_RIGHT_PIN = 26;
    static const unsigned BUTTON_MID_PIN = 13;
    static const unsigned BUTTON_KEY1_PIN = 21;
    static const unsigned BUTTON_KEY2_PIN = 20;
    static const unsigned BUTTON_KEY3_PIN = 16;
    
    // GPIO button arrays for consistent use throughout the program
    static const unsigned GPIO_BUTTON_PINS[NUM_GPIO_BUTTONS];
    static const char* GPIO_BUTTON_LABELS[NUM_GPIO_BUTTONS];
    
    // Helper method to read a button state
    bool IsButtonPressed(unsigned nButtonIndex);

private:
    CSH1106Display  *m_pSH1106Display;

    unsigned m_nColumns;
    unsigned m_nRows;
    unsigned m_nCharW;
    unsigned m_nCharH;
    const TFont &m_rFont;
    bool     m_bDoubleWidth;
    bool     m_bDoubleHeight;
};

#endif
