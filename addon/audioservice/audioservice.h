//
// audioservice.h
//
// Audio Service for Circle
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
#ifndef _audioservice_h
#define _audioservice_h

#include <circle/interrupt.h>
#include <circle/i2cmaster.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/screen.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/usbsoundbasedevice.h>

class CAudioService
{
public:
    CAudioService(CInterruptSystem *pInterruptSystem);
    ~CAudioService(void);

    boolean Initialize();
    boolean IsInitialized(void) const;
    void RequestInitialization(void);
    boolean IsInitRequested(void) const;

    CSoundBaseDevice *GetSoundDevice(void) const;
    static CAudioService *Get(void);

private:
    CInterruptSystem *m_pInterrupt;
    CI2CMaster m_I2CMaster;
    CSoundBaseDevice *m_pSound;
    CScreenDevice *m_pHDMIScreen;
    boolean m_bInitialized;
    volatile boolean m_bInitRequested;
    static CAudioService *s_pThis;
};

#endif
