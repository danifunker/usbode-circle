//
// sh1106display.h
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
#ifndef _display_sh1106display_h
#define _display_sh1106display_h

#include <circle/display.h>
#include <circle/gpioclock.h>
#include <circle/gpiopin.h>
#include <circle/spimaster.h>
#include <circle/spinlock.h>
#include <circle/types.h>
#include <circle/chargenerator.h>

// SH1106 Display Constants
#define SH1106_BLACK_COLOR      0
#define SH1106_WHITE_COLOR      1

// SH1106 Commands
#define SH1106_SETCONTRAST          0x81
#define SH1106_DISPLAYALLON_RESUME  0xA4
#define SH1106_DISPLAYALLON         0xA5
#define SH1106_NORMALDISPLAY        0xA6
#define SH1106_INVERTDISPLAY        0xA7
#define SH1106_DISPLAYOFF           0xAE
#define SH1106_DISPLAYON            0xAF
#define SH1106_SETDISPLAYOFFSET     0xD3
#define SH1106_SETCOMPINS           0xDA
#define SH1106_SETVCOMDETECT        0xDB
#define SH1106_SETDISPLAYCLOCKDIV   0xD5
#define SH1106_SETPRECHARGE         0xD9
#define SH1106_SETMULTIPLEX         0xA8
#define SH1106_SETLOWCOLUMN         0x00
#define SH1106_SETHIGHCOLUMN        0x10
#define SH1106_SETSTARTLINE         0x40
#define SH1106_MEMORYMODE           0x20
#define SH1106_COLUMNADDR           0x21
#define SH1106_PAGEADDR             0x22
#define SH1106_COMSCANINC           0xC0
#define SH1106_COMSCANDEC           0xC8
#define SH1106_SEGREMAP             0xA0
#define SH1106_CHARGEPUMP           0x8D
#define SH1106_SWITCHCAPVCC         0x2
#define SH1106_NOP                  0xE3

// Scrolling commands
#define SH1106_ACTIVATE_SCROLL                      0x2F
#define SH1106_DEACTIVATE_SCROLL                    0x2E
#define SH1106_SET_VERTICAL_SCROLL_AREA             0xA3
#define SH1106_RIGHT_HORIZONTAL_SCROLL              0x26
#define SH1106_LEFT_HORIZONTAL_SCROLL               0x27
#define SH1106_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29
#define SH1106_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL  0x2A

class CSH1106Display : public CDisplay
{
public:
    // No GPIO pin connected (e.g. tied to VDD)

    static const unsigned None = GPIO_PINS;

    typedef u8 TSH1106Color;

    // SH1106 Display configuration
    static const unsigned OLED_WIDTH = 128;
    static const unsigned OLED_HEIGHT = 64;

    // SPI Configuration
    static const unsigned SPI_CLOCK_SPEED = 40000000;  // 40 MHz
    static const unsigned SPI_CPOL = 0;
    static const unsigned SPI_CPHA = 0;
    static const unsigned SPI_CHIP_SELECT = 0;  // CS0

    // GPIO Pin Configuration 
    static const unsigned DC_PIN = 24;     // Data/Command pin
    static const unsigned RESET_PIN = 25;  // Reset pin

    // Button configuration
    static const unsigned NUM_PINS = 8;
    static const unsigned BUTTON_PINS[NUM_PINS];

    // Display text configuration
    static const unsigned DISPLAY_COLUMNS = 21;  // Number of character columns (using 8x8 font)
    static const unsigned DISPLAY_ROWS = 8;      // Number of character rows (using 8x8 font)

    // Constructor and methods of your existing class
    CSH1106Display(CSPIMaster *pSPIMaster, unsigned nDCPin, unsigned nResetPin, 
                  unsigned nWidth, unsigned nHeight, 
                  unsigned nClockSpeed, unsigned nClockPolarity, 
                  unsigned nClockPhase, unsigned nChipSelect);

    ~CSH1106Display(void);

    boolean Initialize(void);

    // Display control
    void On(void);
    void Off(void);
    void Clear(TSH1106Color Color = SH1106_BLACK_COLOR);
    void SetContrast(u8 ucContrast);
    void InvertDisplay(boolean bInvert);
    
    // Add this public method to refresh the display
    void Refresh(void) { UpdateDisplay(); }

    // Drawing primitives
    void SetPixel(unsigned nPosX, unsigned nPosY, TSH1106Color Color);
    void DrawLine(int x0, int y0, int x1, int y1, TSH1106Color Color);

    // Text output
    void DrawText(unsigned nPosX, unsigned nPosY, const char *pString,
                 TSH1106Color Color, TSH1106Color BgColor = SH1106_BLACK_COLOR,
                 bool bDoubleWidth = TRUE, bool bDoubleHeight = TRUE,
                 const TFont &rFont = Font8x8);

    // Low-level implementation of CDisplay interface
    void SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor) override;
    
    void SetArea(const TArea &rArea, const void *pPixels, 
                TAreaCompletionRoutine *pRoutine = nullptr,
                void *pParam = nullptr) override;

    /// \return Number of bits per pixel
    unsigned GetDepth(void) const override { return 1; }  // 1-bit per pixel

    // Implement pure virtual methods from CDisplay
    unsigned GetWidth(void) const override { return m_nWidth; }
    unsigned GetHeight(void) const override { return m_nHeight; }

    // Add these methods to the public section of the CSH1106Display class
    void DrawCircle(int xc, int yc, int radius, TSH1106Color Color);
    void DrawFilledCircle(int xc, int yc, int radius, TSH1106Color Color);
    void DrawCirclePoints(int xc, int yc, int x, int y, TSH1106Color Color);
    void DrawRing(int xc, int yc, int outer_radius, int inner_radius, TSH1106Color Color);
    void DrawRect(int x, int y, int width, int height, TSH1106Color Color);
    void DrawFilledRect(int x, int y, int width, int height, TSH1106Color Color);

private:
    // Send a command to the display
    void SendCommand(u8 ucCommand);
    
    // Send data to the display
    void SendData(const void *pData, size_t nLength);
    
    // Send a single byte
    void SendByte(u8 uchByte, boolean bIsData);
    
    // Update the display buffer to the screen
    void UpdateDisplay(void);
    
    // Set current page and column address for writing
    void SetPosition(unsigned nPage, unsigned nColumn);

private:
    CSPIMaster *m_pSPIMaster;
    unsigned m_nWidth;
    unsigned m_nHeight;
    unsigned m_nResetPin;
    unsigned m_nSPIClockSpeed;
    unsigned m_nSPICPOL;  // Change from m_nSPIMode
    unsigned m_nSPICPHA;  // Add CPHA as separate variable
    unsigned m_nChipSelect;
    
    u8 *m_pFrameBuffer;      // display buffer
    unsigned m_nBufferSize;  // buffer size in bytes
    
    // GPIO pins
    CGPIOPin m_DCPin;        // Data/Command pin
    CGPIOPin m_ResetPin;     // Reset pin
};

#endif