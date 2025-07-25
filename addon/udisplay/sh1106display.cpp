//
// sh1106display.cpp
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
#include "sh1106display.h"
#include <circle/chargenerator.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <circle/new.h>
#include <assert.h>

static const char FromSH1106[] = "sh1106";

// Initialize the static button pins array
const unsigned CSH1106Display::BUTTON_PINS[NUM_PINS] = {6, 19, 5, 26, 13, 21, 20, 16};

CSH1106Display::CSH1106Display(CSPIMaster *pSPIMaster, unsigned nDCPin, unsigned nResetPin,
                            unsigned nWidth, unsigned nHeight, 
                            unsigned nSPIClockSpeed, unsigned nSPICPOL, unsigned nSPICPHA, unsigned nChipSelect)
    : CDisplay(I1),  // Use ColorMono1 instead of MonoColor1
      m_pSPIMaster(pSPIMaster),
      m_nWidth(nWidth),
      m_nHeight(nHeight),
      m_nResetPin(nResetPin),
      m_nSPIClockSpeed(nSPIClockSpeed),
      m_nSPICPOL(nSPICPOL),
      m_nSPICPHA(nSPICPHA),
      m_nChipSelect(nChipSelect),
      m_DCPin(nDCPin, GPIOModeOutput),
      m_ResetPin(nResetPin != None ? nResetPin : GPIO_PINS, GPIOModeOutput)
{
    m_nBufferSize = (m_nWidth * m_nHeight) / 8;  // 1 bit per pixel
    m_pFrameBuffer = new u8[m_nBufferSize];
    
    memset(m_pFrameBuffer, 0, m_nBufferSize);
}

CSH1106Display::~CSH1106Display(void)
{
    delete[] m_pFrameBuffer;
    m_pFrameBuffer = 0;
}

boolean CSH1106Display::Initialize(void)
{
    assert(m_pSPIMaster != 0);
    
    // Hardware reset if reset pin is connected
    if (m_nResetPin != None)
    {
        m_ResetPin.Write(1);
        CTimer::Get()->MsDelay(10);
        m_ResetPin.Write(0);
        CTimer::Get()->MsDelay(10);
        m_ResetPin.Write(1);
        CTimer::Get()->MsDelay(100);
    }
    
    // Initialization sequence specifically for SH1106
    SendCommand(SH1106_DISPLAYOFF);            // 0xAE - Turn off display
    SendCommand(0x02);                         // Set low column address (offset)
    SendCommand(0x10);                         // Set high column address
    SendCommand(SH1106_SETSTARTLINE);          // 0x40 - Set start line address
    SendCommand(SH1106_SETCONTRAST);           // 0x81 - Set contrast control
    SendCommand(0xCF);                         // Contrast value (0-255)
    SendCommand(SH1106_SEGREMAP);              // 0xA0 - Normal orientation (changed from 0xA1)
    SendCommand(SH1106_COMSCANINC);            // 0xC0 - Normal COM scan direction (changed from COMSCANDEC)
    SendCommand(SH1106_NORMALDISPLAY);         // 0xA6 - Normal display (not inverted)
    SendCommand(SH1106_SETMULTIPLEX);          // 0xA8 - Set multiplex ratio
    SendCommand(0x3F);                         // 64 MUX
    SendCommand(SH1106_SETDISPLAYOFFSET);      // 0xD3 - Set display offset
    SendCommand(0x00);                         // No offset
    SendCommand(SH1106_SETDISPLAYCLOCKDIV);    // 0xD5 - Set display clock divide ratio
    SendCommand(0x80);                         // Recommended value
    SendCommand(SH1106_SETPRECHARGE);          // 0xD9 - Set pre-charge period
    SendCommand(0xF1);                         // Recommended value for SH1106
    SendCommand(SH1106_SETCOMPINS);            // 0xDA - Set COM pins hardware configuration
    SendCommand(0x12);                         // Alternative COM pin configuration
    SendCommand(SH1106_SETVCOMDETECT);         // 0xDB - Set VCOMH deselect level
    SendCommand(0x40);                         // 0.77 x Vcc
    SendCommand(0x30);                         // Set pump voltage value (SH1106 specific)
    SendCommand(SH1106_MEMORYMODE);            // 0x20 - Set Memory Addressing Mode
    SendCommand(0x02);                         // Page addressing mode
    
    // Clear display memory
    Clear();
    
    // Turn on the display
    On();
    
    return TRUE;
}

void CSH1106Display::On(void)
{
    SendCommand(SH1106_DISPLAYON);
}

void CSH1106Display::Off(void)
{
    SendCommand(SH1106_DISPLAYOFF);
}

void CSH1106Display::Clear(TSH1106Color Color)
{
    // Fill buffer with 0 for black or 0xFF for white
    memset(m_pFrameBuffer, Color == SH1106_BLACK_COLOR ? 0x00 : 0xFF, m_nBufferSize);
    
    // Update the display
    UpdateDisplay();
}

void CSH1106Display::SetContrast(u8 ucContrast)
{
    SendCommand(SH1106_SETCONTRAST);
    SendCommand(ucContrast);
}

void CSH1106Display::InvertDisplay(boolean bInvert)
{
    if (bInvert)
    {
        SendCommand(SH1106_INVERTDISPLAY);
    }
    else
    {
        SendCommand(SH1106_NORMALDISPLAY);
    }
}

void CSH1106Display::SetPixel(unsigned nPosX, unsigned nPosY, TSH1106Color Color)
{
    // Check bounds
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
    {
        return;
    }
    
    // Calculate byte position and bit offset
    unsigned nByte = (nPosY / 8) * m_nWidth + nPosX;
    unsigned nBit = nPosY % 8;
    
    // Set or clear the bit based on color
    if (Color == SH1106_WHITE_COLOR)
    {
        m_pFrameBuffer[nByte] |= (1 << nBit);
    }
    else
    {
        m_pFrameBuffer[nByte] &= ~(1 << nBit);
    }
}

void CSH1106Display::DrawText(unsigned nPosX, unsigned nPosY, const char *pString,
                            TSH1106Color Color, TSH1106Color BgColor,
                            bool bDoubleWidth, bool bDoubleHeight, const TFont &rFont)
{
    if (pString == 0)
	    return;
    
    CCharGenerator CharGen(rFont, CCharGenerator::MakeFlags(bDoubleWidth, bDoubleHeight));
    
    unsigned nCharWidth = CharGen.GetCharWidth();
    unsigned nCharHeight = CharGen.GetCharHeight();
    
    // Draw all characters first, then update display once at the end
    boolean bModified = FALSE;
    
    while (*pString)
    {
        // Draw each pixel of the character
        for (unsigned nY = 0; nY < nCharHeight; nY++)
        {
            for (unsigned nX = 0; nX < nCharWidth; nX++)
            {
                boolean bPixelOn = CharGen.GetPixel(*pString, nX, nY);
                TSH1106Color PixelColor = (bPixelOn ? Color : BgColor);
                
                // Only set the pixel if it's within display bounds
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
    
    // Only update display if something was modified
    if (bModified)
    {
        UpdateDisplay();
    }
}

void CSH1106Display::SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor)
{
    // Explicitly cast to TSH1106Color to avoid ambiguity
    SetPixel(nPosX, nPosY, nColor != 0 ? 
             static_cast<TSH1106Color>(SH1106_WHITE_COLOR) : 
             static_cast<TSH1106Color>(SH1106_BLACK_COLOR));
}

void CSH1106Display::SetArea(const TArea &rArea, const void *pPixels,
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
            
            TSH1106Color Color = (pBuffer[nByte] & (1 << nBit)) ? SH1106_WHITE_COLOR : SH1106_BLACK_COLOR;
            SetPixel(nPosX, nPosY, Color);
        }
    }
    
    // Update display after setting area
    UpdateDisplay();
    
    if (pRoutine != 0)
    {
        (*pRoutine)(pParam);
    }
}

void CSH1106Display::SendCommand(u8 ucCommand)
{
    SendByte(ucCommand, FALSE);
}

void CSH1106Display::SendData(const void *pData, size_t nLength)
{
    assert(pData != 0);
    assert(m_pSPIMaster != 0);
    
    m_DCPin.Write(1);  // Data mode
    
    m_pSPIMaster->SetClock(m_nSPIClockSpeed);
    m_pSPIMaster->SetMode(m_nSPICPOL, m_nSPICPHA);  // Pass both parameters
    
    // Cast the result to size_t to avoid signed/unsigned comparison warning
    assert((size_t)m_pSPIMaster->Write(m_nChipSelect, pData, nLength) == nLength);
}

void CSH1106Display::SendByte(u8 uchByte, boolean bIsData)
{
    assert(m_pSPIMaster != 0);
    
    m_DCPin.Write(bIsData ? 1 : 0);  // Data or Command mode
    
    m_pSPIMaster->SetClock(m_nSPIClockSpeed);
    m_pSPIMaster->SetMode(m_nSPICPOL, m_nSPICPHA);  // Pass both parameters
    
    assert((size_t)m_pSPIMaster->Write(m_nChipSelect, &uchByte, 1) == 1);
}

void CSH1106Display::UpdateDisplay(void)
{
    // SH1106 needs to update by pages (8 pixels high)
    unsigned nPages = m_nHeight / 8;
    
    // Use a static buffer to avoid repeated allocations
    static u8 pageBuffer[128]; // Maximum width
    
    for (unsigned nPage = 0; nPage < nPages; nPage++)
    {
        // Set page and starting column
        SetPosition(nPage, 0);
        
        // Copy data from framebuffer to page buffer with correct orientation
        for (unsigned i = 0; i < m_nWidth; i++)
        {
            // No need for inversion - just copy the data directly
            pageBuffer[i] = m_pFrameBuffer[nPage * m_nWidth + i];
        }
        
        // Send a full page of data
        SendData(pageBuffer, m_nWidth);
    }
}

void CSH1106Display::SetPosition(unsigned nPage, unsigned nColumn)
{
    // The SH1106 has 132x64 internal RAM, but only 128x64 is visible
    // Using a standard 2-pixel offset to center the display
    const unsigned nColumnOffset = 2;
    
    SendCommand(0xB0 | (nPage & 0x0F));                       // Set page address
    SendCommand(0x00 | ((nColumn + nColumnOffset) & 0x0F));   // Set lower column address
    SendCommand(0x10 | (((nColumn + nColumnOffset) >> 4) & 0x0F));  // Set higher column address
}

// Add this implementation
void CSH1106Display::DrawLine(int x0, int y0, int x1, int y1, TSH1106Color Color)
{
    // Bresenham's line algorithm
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);  // Manual abs implementation
    int sx = x0 < x1 ? 1 : -1;
    int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);  // Manual abs implementation with negation
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

// Add these methods to the CSH1106Display class

void CSH1106Display::DrawCircle(int xc, int yc, int radius, TSH1106Color Color)
{
    // Bresenham's circle algorithm
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

void CSH1106Display::DrawFilledCircle(int xc, int yc, int radius, TSH1106Color Color)
{
    // Draw a filled circle using horizontal lines
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x*x + y*y <= radius*radius)
            {
                SetPixel(xc + x, yc + y, Color);
            }
        }
    }
}

void CSH1106Display::DrawCirclePoints(int xc, int yc, int x, int y, TSH1106Color Color)
{
    // Helper function for DrawCircle - plots the 8 points of the circle
    SetPixel(xc + x, yc + y, Color);
    SetPixel(xc - x, yc + y, Color);
    SetPixel(xc + x, yc - y, Color);
    SetPixel(xc - x, yc - y, Color);
    SetPixel(xc + y, yc + x, Color);
    SetPixel(xc - y, yc + x, Color);
    SetPixel(xc + y, yc - x, Color);
    SetPixel(xc - y, yc - x, Color);
}

void CSH1106Display::DrawRing(int xc, int yc, int outer_radius, int inner_radius, TSH1106Color Color)
{
    // Draw a ring (hollow circle with inner and outer radius)
    for (int y = -outer_radius; y <= outer_radius; y++)
    {
        for (int x = -outer_radius; x <= outer_radius; x++)
        {
            int distance_squared = x*x + y*y;
            if (distance_squared <= outer_radius*outer_radius && 
                distance_squared >= inner_radius*inner_radius)
            {
                SetPixel(xc + x, yc + y, Color);
            }
        }
    }
}

void CSH1106Display::DrawRect(int x, int y, int width, int height, TSH1106Color Color)
{
    // Draw a rectangle outline
    DrawLine(x, y, x + width - 1, y, Color);
    DrawLine(x, y + height - 1, x + width - 1, y + height - 1, Color);
    DrawLine(x, y, x, y + height - 1, Color);
    DrawLine(x + width - 1, y, x + width - 1, y + height - 1, Color);
}

void CSH1106Display::DrawFilledRect(int x, int y, int width, int height, TSH1106Color Color)
{
    // Draw a filled rectangle
    for (int j = y; j < y + height; j++)
    {
        for (int i = x; i < x + width; i++)
        {
            SetPixel(i, j, Color);
        }
    }
}
