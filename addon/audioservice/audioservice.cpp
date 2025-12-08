//
// audioservice.cpp
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
#include "audioservice.h"
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <configservice/configservice.h>
#include <circle/util.h>

LOGMODULE("audioservice");

#define SAMPLE_RATE 44100
#define SOUND_CHUNK_SIZE (384 * 10)
#define WRITE_CHANNELS 2
#define FORMAT SoundFormatSigned16
#define DAC_I2C_ADDRESS 0

// DAC_BUFFER_SIZE_FRAMES matches what CCDPlayer expects/uses
// SECTOR_SIZE 2352, BATCH_SIZE 16, BYTES_PER_FRAME 4
// FRAMES_PER_SECTOR (2352/4) = 588
// DAC_BUFFER_SIZE_FRAMES = 588 * 16 = 9408
#define DAC_BUFFER_SIZE_FRAMES 9408

CAudioService *CAudioService::s_pThis = nullptr;

CAudioService::CAudioService(CInterruptSystem *pInterruptSystem)
    : m_pInterrupt(pInterruptSystem),
      m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), FALSE),
      m_pSound(nullptr),
      m_pHDMIScreen(nullptr),
      m_bInitialized(FALSE)
{
    s_pThis = this;
}

CAudioService::~CAudioService(void)
{
    if (m_pSound)
    {
        delete m_pSound;
        m_pSound = nullptr;
    }
    if (m_pHDMIScreen)
    {
        delete m_pHDMIScreen;
        m_pHDMIScreen = nullptr;
    }
    s_pThis = nullptr;
}

boolean CAudioService::IsInitialized(void) const
{
    return m_bInitialized;
}

boolean CAudioService::Initialize()
{
    if (m_bInitialized) {
        return TRUE;
    }

    LOGNOTE("Audio Service Initializing I2CMaster");
    if (!m_I2CMaster.Initialize())
    {
        LOGERR("Failed to initialize I2C Master");
        return FALSE;
    }

    ConfigService *config = ConfigService::Get();
    const char *pSoundDevice = config->GetSoundDev();

    if (strcmp(pSoundDevice, "sndpwm") == 0)
    {
        m_pSound = new CPWMSoundBaseDevice(m_pInterrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
        LOGNOTE("Audio Service Initializing sndpwm");
    }
    else if (strcmp(pSoundDevice, "sndi2s") == 0)
    {
        m_pSound = new CI2SSoundBaseDevice(m_pInterrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE, FALSE,
                                           &m_I2CMaster, DAC_I2C_ADDRESS);
        LOGNOTE("Audio Service Initializing sndi2s");
    }
    else if (strcmp(pSoundDevice, "sndhdmi") == 0)
    {
        // Initialize basic HDMI display to enable audio
        // Use Circle's screen device to activate HDMI
        m_pHDMIScreen = new CScreenDevice(1920, 1080); // false = no console output
        if (m_pHDMIScreen->Initialize())
        {
            LOGNOTE("HDMI display initialized for audio support");
            m_pSound = new CHDMISoundBaseDevice(m_pInterrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
            LOGNOTE("Audio Service Initializing sndhdmi");
        }
        else
        {
            LOGERR("Failed to initialize HDMI display - HDMI audio not available");
            return FALSE;
        }
    }
#if RASPPI >= 4
    else if (strcmp(pSoundDevice, "sndusb") == 0)
    {
        m_pSound = new CUSBSoundBaseDevice(SAMPLE_RATE);
        LOGNOTE("Audio Service Initializing sndusb");
    }
#endif
    else
    {
        LOGWARN("Unknown or unsupported sound device: %s", pSoundDevice);
        return FALSE;
    }

    if (!m_pSound)
    {
        LOGERR("Failed to create sound device");
        return FALSE;
    }

    LOGNOTE("Audio Service Initializing. Allocating queue size %d frames", DAC_BUFFER_SIZE_FRAMES);
    if (!m_pSound->AllocateQueueFrames(DAC_BUFFER_SIZE_FRAMES))
    {
        LOGERR("Cannot allocate sound queue");
        return FALSE;
    }

    m_pSound->SetWriteFormat(FORMAT, WRITE_CHANNELS);

    // We start the sound device immediately.
    // The previous implementation had lazy init but the requirement is to have it self-contained
    // and started by kernel.
    if (!m_pSound->Start())
    {
        LOGERR("Couldn't start the sound device");
        return FALSE;
    }

    m_bInitialized = TRUE;
    LOGNOTE("Audio Service started successfully");
    return TRUE;
}

CSoundBaseDevice *CAudioService::GetSoundDevice(void) const
{
    return m_pSound;
}

CAudioService *CAudioService::Get(void)
{
    return s_pThis;
}
