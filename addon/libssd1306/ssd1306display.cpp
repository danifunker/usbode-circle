//
// ssd1306display.cpp
//
// Graphics-capable SSD1306 (I2C) display driver for USBODE.
// See ssd1306display.h for rationale. Drawing primitives mirror
// libsh1106/sh1106display.cpp; transport is I2C and the init sequence
// follows the SSD1306 datasheet (cf. Circle's addon/display/ssd1306device.cpp).
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
#include "ssd1306display.h"
#include <circle/chargenerator.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <circle/new.h>
#include <assert.h>

static const char FromSSD1306[] = "ssd1306";

CSSD1306GfxDisplay::CSSD1306GfxDisplay(CI2CMaster *pI2CMaster, u8 nAddress,
                                       unsigned nWidth, unsigned nHeight)
    : CDisplay(I1),  // 1 bit per pixel, monochrome
      m_pI2CMaster(pI2CMaster),
      m_nAddress(nAddress),
      m_nWidth(nWidth),
      m_nHeight(nHeight)
{
    m_nBufferSize = (m_nWidth * m_nHeight) / 8;  // 1 bit per pixel
    m_pFrameBuffer = new u8[m_nBufferSize];
    memset(m_pFrameBuffer, 0, m_nBufferSize);
}

CSSD1306GfxDisplay::~CSSD1306GfxDisplay(void)
{
    delete[] m_pFrameBuffer;
    m_pFrameBuffer = nullptr;
}

boolean CSSD1306GfxDisplay::Initialize(void)
{
    assert(m_pI2CMaster != 0);

    // Only 128x32 and 128x64 supported
    if (m_nWidth != 128 || !(m_nHeight == 32 || m_nHeight == 64))
        return FALSE;

    const u8 nMultiplexRatio = m_nHeight - 1;
    const u8 nCOMPins        = (m_nHeight == 32) ? 0x02 : 0x12;

    // SSD1306 initialization sequence (horizontal addressing mode)
    SendCommand(SSD1306_DISPLAYOFF);
    SendCommand(SSD1306_SETDISPLAYCLOCKDIV);
    SendCommand(0x80);                       // Default ratio/oscillator
    SendCommand(SSD1306_SETMULTIPLEX);
    SendCommand(nMultiplexRatio);            // Screen height - 1
    SendCommand(SSD1306_SETDISPLAYOFFSET);
    SendCommand(0x00);                       // No offset
    SendCommand(SSD1306_SETSTARTLINE | 0x00);
    SendCommand(SSD1306_CHARGEPUMP);
    SendCommand(0x14);                       // Enable internal charge pump
    SendCommand(SSD1306_MEMORYMODE);
    SendCommand(0x00);                       // Horizontal addressing mode
    SendCommand(SSD1306_SEGREMAP | 0x01);    // Column 127 mapped to SEG0
    SendCommand(SSD1306_COMSCANDEC);         // Scan from COM[N-1] to COM0
    SendCommand(SSD1306_SETCOMPINS);
    SendCommand(nCOMPins);
    SendCommand(SSD1306_SETCONTRAST);
    SendCommand(0xCF);
    SendCommand(SSD1306_SETPRECHARGE);
    SendCommand(0xF1);
    SendCommand(SSD1306_SETVCOMDETECT);
    SendCommand(0x40);
    SendCommand(SSD1306_DISPLAYALLON_RESUME);
    SendCommand(SSD1306_NORMALDISPLAY);

    // Clear display memory and turn on
    Clear();
    On();

    return TRUE;
}

void CSSD1306GfxDisplay::On(void)
{
    SendCommand(SSD1306_DISPLAYON);
}

void CSSD1306GfxDisplay::Off(void)
{
    SendCommand(SSD1306_DISPLAYOFF);
}

void CSSD1306GfxDisplay::Clear(TSSD1306Color Color)
{
    memset(m_pFrameBuffer, Color == SSD1306_BLACK_COLOR ? 0x00 : 0xFF, m_nBufferSize);
    UpdateDisplay();
}

void CSSD1306GfxDisplay::SetContrast(u8 ucContrast)
{
    SendCommand(SSD1306_SETCONTRAST);
    SendCommand(ucContrast);
}

void CSSD1306GfxDisplay::InvertDisplay(boolean bInvert)
{
    SendCommand(bInvert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
}

void CSSD1306GfxDisplay::SetRotation(boolean bRotate180)
{
    if (bRotate180)
    {
        // Mirror columns and reverse COM scan -> panel appears upside down
        SendCommand(SSD1306_SEGREMAP | 0x00);
        SendCommand(SSD1306_COMSCANINC);
    }
    else
    {
        // Default orientation, matching Initialize()
        SendCommand(SSD1306_SEGREMAP | 0x01);
        SendCommand(SSD1306_COMSCANDEC);
    }
}

void CSSD1306GfxDisplay::SetPixel(unsigned nPosX, unsigned nPosY, TSSD1306Color Color)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
        return;

    // 1bpp, page-packed: 8 vertical pixels per byte
    unsigned nByte = (nPosY / 8) * m_nWidth + nPosX;
    unsigned nBit = nPosY % 8;

    if (Color == SSD1306_WHITE_COLOR)
        m_pFrameBuffer[nByte] |= (1 << nBit);
    else
        m_pFrameBuffer[nByte] &= ~(1 << nBit);
}

void CSSD1306GfxDisplay::DrawText(unsigned nPosX, unsigned nPosY, const char *pString,
                                  TSSD1306Color Color, TSSD1306Color BgColor,
                                  bool bDoubleWidth, bool bDoubleHeight, const TFont &rFont)
{
    if (pString == 0)
        return;

    CCharGenerator CharGen(rFont, CCharGenerator::MakeFlags(bDoubleWidth, bDoubleHeight));

    unsigned nCharWidth = CharGen.GetCharWidth();
    unsigned nCharHeight = CharGen.GetCharHeight();

    boolean bModified = FALSE;

    while (*pString)
    {
        for (unsigned nY = 0; nY < nCharHeight; nY++)
        {
            for (unsigned nX = 0; nX < nCharWidth; nX++)
            {
                boolean bPixelOn = CharGen.GetPixel(*pString, nX, nY);
                TSSD1306Color PixelColor = (bPixelOn ? Color : BgColor);

                if ((nPosX + nX < m_nWidth) && (nPosY + nY < m_nHeight))
                {
                    SetPixel(nPosX + nX, nPosY + nY, PixelColor);
                    bModified = TRUE;
                }
            }
        }

        nPosX += nCharWidth;
        pString++;
    }

    if (bModified)
        UpdateDisplay();
}

void CSSD1306GfxDisplay::SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor)
{
    SetPixel(nPosX, nPosY, nColor != 0 ?
             static_cast<TSSD1306Color>(SSD1306_WHITE_COLOR) :
             static_cast<TSSD1306Color>(SSD1306_BLACK_COLOR));
}

void CSSD1306GfxDisplay::SetArea(const TArea &rArea, const void *pPixels,
                                 TAreaCompletionRoutine *pRoutine, void *pParam)
{
    assert(pPixels != 0);

    const u8 *pBuffer = static_cast<const u8 *>(pPixels);

    for (unsigned nPosY = rArea.y1; nPosY <= rArea.y2; nPosY++)
    {
        for (unsigned nPosX = rArea.x1; nPosX <= rArea.x2; nPosX++)
        {
            unsigned nOffset = (nPosY - rArea.y1) * (rArea.x2 - rArea.x1 + 1) + (nPosX - rArea.x1);
            unsigned nByte = nOffset / 8;
            unsigned nBit = 7 - (nOffset % 8);  // MSB first

            TSSD1306Color Color = (pBuffer[nByte] & (1 << nBit)) ? SSD1306_WHITE_COLOR : SSD1306_BLACK_COLOR;
            SetPixel(nPosX, nPosY, Color);
        }
    }

    UpdateDisplay();

    if (pRoutine != 0)
        (*pRoutine)(pParam);
}

void CSSD1306GfxDisplay::SendCommand(u8 ucCommand)
{
    assert(m_pI2CMaster != 0);
    const u8 Buffer[] = { SSD1306_CONTROL_COMMAND, ucCommand };
    m_pI2CMaster->Write(m_nAddress, Buffer, sizeof(Buffer));
}

void CSSD1306GfxDisplay::SendData(const void *pData, size_t nLength)
{
    assert(pData != 0);
    assert(m_pI2CMaster != 0);

    // Prefix the data stream with the 0x40 control byte. Use a stack buffer
    // sized for one page row (max display width).
    u8 Buffer[1 + 128];
    assert(nLength <= 128);

    Buffer[0] = SSD1306_CONTROL_DATA;
    memcpy(&Buffer[1], pData, nLength);

    m_pI2CMaster->Write(m_nAddress, Buffer, nLength + 1);
}

void CSSD1306GfxDisplay::UpdateDisplay(void)
{
    unsigned nPages = m_nHeight / 8;

    for (unsigned nPage = 0; nPage < nPages; nPage++)
    {
        SetPosition(nPage, 0);
        SendData(&m_pFrameBuffer[nPage * m_nWidth], m_nWidth);
    }
}

void CSSD1306GfxDisplay::SetPosition(unsigned nPage, unsigned nColumn)
{
    // SSD1306 has no column offset (unlike SH1106's 132-wide RAM)
    SendCommand(0xB0 | (nPage & 0x0F));               // Set page address
    SendCommand(0x00 | (nColumn & 0x0F));             // Lower column address
    SendCommand(0x10 | ((nColumn >> 4) & 0x0F));      // Higher column address
}

void CSSD1306GfxDisplay::DrawLine(int x0, int y0, int x1, int y1, TSSD1306Color Color)
{
    // Bresenham's line algorithm
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = x0 < x1 ? 1 : -1;
    int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int e2;

    while (true)
    {
        SetPixel(x0, y0, Color);

        if (x0 == x1 && y0 == y1)
            break;

        e2 = 2 * err;
        if (e2 >= dy)
        {
            if (x0 == x1)
                break;
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            if (y0 == y1)
                break;
            err += dx;
            y0 += sy;
        }
    }

    UpdateDisplay();
}

void CSSD1306GfxDisplay::DrawCircle(int xc, int yc, int radius, TSSD1306Color Color)
{
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    DrawCirclePoints(xc, yc, x, y, Color);

    while (y >= x)
    {
        x++;

        if (d > 0)
        {
            y--;
            d = d + 4 * (x - y) + 10;
        }
        else
        {
            d = d + 4 * x + 6;
        }

        DrawCirclePoints(xc, yc, x, y, Color);
    }
}

void CSSD1306GfxDisplay::DrawFilledCircle(int xc, int yc, int radius, TSSD1306Color Color)
{
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x * x + y * y <= radius * radius)
                SetPixel(xc + x, yc + y, Color);
        }
    }
}

void CSSD1306GfxDisplay::DrawCirclePoints(int xc, int yc, int x, int y, TSSD1306Color Color)
{
    SetPixel(xc + x, yc + y, Color);
    SetPixel(xc - x, yc + y, Color);
    SetPixel(xc + x, yc - y, Color);
    SetPixel(xc - x, yc - y, Color);
    SetPixel(xc + y, yc + x, Color);
    SetPixel(xc - y, yc + x, Color);
    SetPixel(xc + y, yc - x, Color);
    SetPixel(xc - y, yc - x, Color);
}

void CSSD1306GfxDisplay::DrawRing(int xc, int yc, int outer_radius, int inner_radius, TSSD1306Color Color)
{
    for (int y = -outer_radius; y <= outer_radius; y++)
    {
        for (int x = -outer_radius; x <= outer_radius; x++)
        {
            int distance_squared = x * x + y * y;
            if (distance_squared <= outer_radius * outer_radius &&
                distance_squared >= inner_radius * inner_radius)
            {
                SetPixel(xc + x, yc + y, Color);
            }
        }
    }
}

void CSSD1306GfxDisplay::DrawRect(int x, int y, int width, int height, TSSD1306Color Color)
{
    DrawLine(x, y, x + width - 1, y, Color);
    DrawLine(x, y + height - 1, x + width - 1, y + height - 1, Color);
    DrawLine(x, y, x, y + height - 1, Color);
    DrawLine(x + width - 1, y, x + width - 1, y + height - 1, Color);
}

void CSSD1306GfxDisplay::DrawFilledRect(int x, int y, int width, int height, TSSD1306Color Color)
{
    for (int j = y; j < y + height; j++)
    {
        for (int i = x; i < x + width; i++)
            SetPixel(i, j, Color);
    }
}
