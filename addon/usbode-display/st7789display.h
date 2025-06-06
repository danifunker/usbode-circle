//
/// \file st7789display.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2021-2024  R. Stange <rsta2@o2online.de>
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
#ifndef _display_st7789display_h
#define _display_st7789display_h

#include <circle/display.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/chargenerator.h>
#include <circle/util.h>
#include <circle/types.h>

class CST7789Display : public CDisplay	/// Driver for ST7789-based dot-matrix displays
{
public:
	static const unsigned None = GPIO_PINS;

	typedef u16 TST7789Color;
// These colors are valid with bSwapColorBytes only.
// really ((green) & 0x3F) << 5, but to have a 0-31 range for all colors
#define ST7789_COLOR(red, green, blue)	  bswap16 (((red) & 0x1F) << 11 \
					| ((green) & 0x1F) << 6 \
					| ((blue) & 0x1F))
#define ST7789_BLACK_COLOR	0
#define ST7789_RED_COLOR	0x00F8
#define ST7789_GREEN_COLOR	0xE007
#define ST7789_BLUE_COLOR	0x1F00
#define ST7789_WHITE_COLOR	0xFFFF

public:
	/// \param pSPIMaster Pointer to SPI master object
	/// \param nDCPin GPIO pin number for DC pin
	/// \param nResetPin GPIO pin number for Reset pin (optional)
	/// \param nBackLightPin GPIO pin number for backlight pin (optional)
	/// \param nWidth Display width in number of pixels (default 240)
	/// \param nHeight Display height in number of pixels (default 240)
	/// \param CPOL SPI clock polarity (0 or 1, default 0)
	/// \param CPHA SPI clock phase (0 or 1, default 0)
	/// \param nClockSpeed SPI clock frequency in Hz
	/// \param nChipSelect SPI chip select (if connected, otherwise don't care)
	/// \param bSwapColorBytes Use big endian colors instead of normal RGB565
	/// \note GPIO pin numbers are SoC number, not header positions.
	/// \note If SPI chip select is not connected, CPOL should probably be 1.
	CST7789Display (CSPIMaster *pSPIMaster,
			unsigned nDCPin, unsigned nResetPin = None, unsigned nBackLightPin = None,
			unsigned nWidth = 240, unsigned nHeight = 240,
			unsigned CPOL = 0, unsigned CPHA = 0, unsigned nClockSpeed = 15000000,
			unsigned nChipSelect = 0, boolean bSwapColorBytes = TRUE);

	~CST7789Display (void);

	/// \return Display width in number of pixels
	unsigned GetWidth (void) const		{ return m_nWidth; }
	/// \return Display height in number of pixels
	unsigned GetHeight (void) const		{ return m_nHeight; }
	/// \return Number of bits per pixels
	unsigned GetDepth (void) const		{ return 16; }

	/// \return Operation successful?
	boolean Initialize (void);

	/// \brief Set the global rotation of the display
	/// \param nRot (0, 90, 180, 270)
	void SetRotation (unsigned nRot);
	/// \return Rotation in degrees (0,90,180,270)
	unsigned GetRotation (void) const	{ return m_nRotation; }

	/// \brief Set display on
	void On (void);
	/// \brief Set display off
	void Off (void);

	/// \brief Clear entire display with color
	/// \param Color RGB565 color with swapped bytes (see: ST7789_COLOR())
	void Clear (TST7789Color Color = ST7789_BLACK_COLOR);

	/// \brief Set a single pixel to color
	/// \param nPosX X-position (0..width-1)
	/// \param nPosY Y-postion (0..height-1)
	/// \param Color RGB565 color with swapped bytes (see: ST7789_COLOR())
	void SetPixel (unsigned nPosX, unsigned nPosY, TST7789Color Color);

	/// \brief Draw an ISO8859-1 string at a specific pixel position
	/// \param nPosX X-position (0..width-1)
	/// \param nPosY Y-postion (0..height-1)
	/// \param pString 0-terminated string of printable characters
	/// \param Color RGB565 foreground color with swapped bytes (see: ST7789_COLOR())
	/// \param BgColor RGB565 background color with swapped bytes (see: ST7789_COLOR())
	/// \param bDoubleWidth default TRUE for thicker characters on screen
	/// \param bDoubleHeight default TRUE for higher characters on screen
	/// \param rFont Font to be used
	void DrawText (unsigned nPosX, unsigned nPosY, const char *pString,
		       TST7789Color Color, TST7789Color BgColor = ST7789_BLACK_COLOR,
		       bool bDoubleWidth = TRUE, bool bDoubleHeight = TRUE,
		       const TFont &rFont = Font8x16);

	/// \brief Set a single pixel to color
	/// \param nPosX X-position (0..width-1)
	/// \param nPosY Y-postion (0..height-1)
	/// \param nColor Raw color value (RGB565 or RGB565_BE)
	void SetPixel (unsigned nPosX, unsigned nPosY, TRawColor nColor);

	/// \brief Set area (rectangle) on the display to the raw colors in pPixels
	/// \param rArea Coordinates of the area (zero-based)
	/// \param pPixels Pointer to array with raw color values (RGB565 or RGB565_BE)
	/// \param pRoutine Routine to be called on completion
	/// \param pParam User parameter to be handed over to completion routine
	void SetArea (const TArea &rArea, const void *pPixels,
		      TAreaCompletionRoutine *pRoutine = nullptr,
		      void *pParam = nullptr);

private:
	void SetWindow (unsigned x0, unsigned y0, unsigned x1, unsigned y1);

	void SendByte (u8 uchByte, boolean bIsData);

	void Command (u8 uchByte)	{ SendByte (uchByte, FALSE); }
	void Data (u8 uchByte)		{ SendByte (uchByte, TRUE); }

	void SendData (const void *pData, size_t nLength);
	
	unsigned RotX (unsigned x, unsigned y);
	unsigned RotY (unsigned x, unsigned y);

private:
	CSPIMaster *m_pSPIMaster;
	unsigned m_nResetPin;
	unsigned m_nBackLightPin;
	unsigned m_nWidth;
	unsigned m_nHeight;
	unsigned m_CPOL;
	unsigned m_CPHA;
	unsigned m_nClockSpeed;
	unsigned m_nChipSelect;
	boolean m_bSwapColorBytes;

	unsigned m_nRotation;
	u16 *m_pBuffer;

	CGPIOPin m_DCPin;
	CGPIOPin m_ResetPin;
	CGPIOPin m_BackLightPin;
};

#endif
