//
// ssd1306display.h
//
// Graphics-capable SSD1306 (I2C) display driver for USBODE.
//
// This is a CDisplay subclass so it can be driven by Circle's C2DGraphics,
// exactly like the SPI-based CSH1106Display in libsh1106. It is intentionally
// API-compatible with CSH1106Display so the display-service pages can be shared
// with only a type/include change.
//
// NOTE: Circle ships an SSD1306 driver (addon/display/ssd1306device.*) but that
// one is a CCharDevice (text only) and cannot be used with C2DGraphics. The
// SSD1306 init sequence and I2C control-byte protocol used here are based on it.
//
// Copyright (C) 2025
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
#ifndef _display_ssd1306display_h
#define _display_ssd1306display_h

#include <circle/display.h>
#include <circle/i2cmaster.h>
#include <circle/types.h>
#include <circle/chargenerator.h>

// SSD1306 colors (1bpp)
#define SSD1306_BLACK_COLOR 0
#define SSD1306_WHITE_COLOR 1

// SSD1306 commands (only the ones we use)
#define SSD1306_SETCONTRAST          0x81
#define SSD1306_DISPLAYALLON_RESUME  0xA4
#define SSD1306_NORMALDISPLAY        0xA6
#define SSD1306_INVERTDISPLAY        0xA7
#define SSD1306_DISPLAYOFF           0xAE
#define SSD1306_DISPLAYON            0xAF
#define SSD1306_SETDISPLAYOFFSET     0xD3
#define SSD1306_SETCOMPINS           0xDA
#define SSD1306_SETVCOMDETECT        0xDB
#define SSD1306_SETDISPLAYCLOCKDIV   0xD5
#define SSD1306_SETPRECHARGE         0xD9
#define SSD1306_SETMULTIPLEX         0xA8
#define SSD1306_SETSTARTLINE         0x40
#define SSD1306_MEMORYMODE           0x20
#define SSD1306_COLUMNADDR           0x21
#define SSD1306_PAGEADDR             0x22
#define SSD1306_COMSCANINC           0xC0
#define SSD1306_COMSCANDEC           0xC8
#define SSD1306_SEGREMAP             0xA0
#define SSD1306_CHARGEPUMP           0x8D

// I2C control bytes (Co=0, D/C# selects command vs data stream)
#define SSD1306_CONTROL_COMMAND      0x00
#define SSD1306_CONTROL_DATA         0x40

class CSSD1306GfxDisplay : public CDisplay
{
public:
    typedef u8 TSSD1306Color;

    // Default I2C slave address of the display controller
    static const u8 DEFAULT_I2C_ADDRESS = 0x3C;

    // Nominal panel geometry (used by shared pages for bounds checks).
    // The runtime width/height are taken from the constructor.
    static const unsigned OLED_WIDTH = 128;
    static const unsigned OLED_HEIGHT = 64;

    // Constructor.
    // \param pI2CMaster  I2C master to be used (already constructed)
    // \param nAddress    I2C slave address (0x3C or 0x3D)
    // \param nWidth      Display width in pixels (128)
    // \param nHeight     Display height in pixels (32 or 64)
    CSSD1306GfxDisplay(CI2CMaster *pI2CMaster, u8 nAddress,
                       unsigned nWidth, unsigned nHeight);

    ~CSSD1306GfxDisplay(void);

    boolean Initialize(void);

    // Display control
    void On(void);
    void Off(void);
    void Clear(TSSD1306Color Color = SSD1306_BLACK_COLOR);
    void SetContrast(u8 ucContrast);
    void InvertDisplay(boolean bInvert);
    // Rotate the panel 180 degrees by flipping segment remap and COM scan
    // direction. Takes effect immediately; call after Initialize().
    void SetRotation(boolean bRotate180);

    // Refresh the display from the framebuffer
    void Refresh(void) { UpdateDisplay(); }

    // Drawing primitives
    void SetPixel(unsigned nPosX, unsigned nPosY, TSSD1306Color Color);
    void DrawLine(int x0, int y0, int x1, int y1, TSSD1306Color Color);

    // Text output
    void DrawText(unsigned nPosX, unsigned nPosY, const char *pString,
                  TSSD1306Color Color, TSSD1306Color BgColor = SSD1306_BLACK_COLOR,
                  bool bDoubleWidth = TRUE, bool bDoubleHeight = TRUE,
                  const TFont &rFont = Font8x8);

    // CDisplay interface
    void SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor) override;
    void SetArea(const TArea &rArea, const void *pPixels,
                 TAreaCompletionRoutine *pRoutine = nullptr,
                 void *pParam = nullptr) override;
    unsigned GetDepth(void) const override { return 1; }
    unsigned GetWidth(void) const override { return m_nWidth; }
    unsigned GetHeight(void) const override { return m_nHeight; }

    // Shape helpers (API-compatible with CSH1106Display)
    void DrawCircle(int xc, int yc, int radius, TSSD1306Color Color);
    void DrawFilledCircle(int xc, int yc, int radius, TSSD1306Color Color);
    void DrawCirclePoints(int xc, int yc, int x, int y, TSSD1306Color Color);
    void DrawRing(int xc, int yc, int outer_radius, int inner_radius, TSSD1306Color Color);
    void DrawRect(int x, int y, int width, int height, TSSD1306Color Color);
    void DrawFilledRect(int x, int y, int width, int height, TSSD1306Color Color);

private:
    void SendCommand(u8 ucCommand);
    void SendData(const void *pData, size_t nLength);
    void UpdateDisplay(void);
    void SetPosition(unsigned nPage, unsigned nColumn);

private:
    CI2CMaster *m_pI2CMaster;
    u8 m_nAddress;
    unsigned m_nWidth;
    unsigned m_nHeight;

    u8 *m_pFrameBuffer;      // display buffer, 1bpp
    unsigned m_nBufferSize;  // buffer size in bytes
};

#endif
