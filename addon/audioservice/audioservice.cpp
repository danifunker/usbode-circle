#include "audioservice.h"
#include <circle/machineinfo.h>
#include <circle/util.h>
#include <circle/sched/scheduler.h>
#include <string.h>

#define DAC_I2C_ADDRESS 0

LOGMODULE("audioservice");

CAudioService::CAudioService(const char *pSoundDevice, CInterruptSystem *pInterrupt)
    : m_pSoundDeviceName(pSoundDevice),
      m_pI2CMaster(nullptr),
      m_pInterrupt(pInterrupt),
      m_pSound(nullptr),
      m_Volume(255),
      m_DefaultVolume(255),
      m_bAudioInitialized(false),
      m_bStartRequested(false)
{
    SetName(AUDIO_SERVICE_NAME);
}

CAudioService::~CAudioService()
{
    if (m_pSound) delete m_pSound;
    if (m_pI2CMaster) delete m_pI2CMaster;
}

boolean CAudioService::Initialize()
{
    // If I2S, we need I2C Master
    if (strcmp(m_pSoundDeviceName, "sndi2s") == 0) {
        LOGNOTE("AudioService: Initializing I2CMaster for I2S");
        m_pI2CMaster = new CI2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), FALSE);
        m_pI2CMaster->Initialize();
    }

    if (strcmp(m_pSoundDeviceName, "sndpwm") == 0) {
        m_pSound = new CPWMSoundBaseDevice(m_pInterrupt, AUDIO_SAMPLE_RATE, AUDIO_CHUNK_SIZE);
        LOGNOTE("AudioService: Initializing sndpwm");
    } else if (strcmp(m_pSoundDeviceName, "sndi2s") == 0) {
        m_pSound = new CI2SSoundBaseDevice(m_pInterrupt, AUDIO_SAMPLE_RATE, AUDIO_CHUNK_SIZE, FALSE,
                                           m_pI2CMaster, DAC_I2C_ADDRESS);
        LOGNOTE("AudioService: Initializing sndi2s");
    } else if (strcmp(m_pSoundDeviceName, "sndhdmi") == 0) {
        m_pSound = new CHDMISoundBaseDevice(m_pInterrupt, AUDIO_SAMPLE_RATE, AUDIO_CHUNK_SIZE);
        LOGNOTE("AudioService: Initializing sndhdmi");
    }
#if RASPPI >= 4
    else if (strcmp(m_pSoundDeviceName, "sndusb") == 0) {
        m_pSound = new CUSBSoundBaseDevice(AUDIO_SAMPLE_RATE);
        LOGNOTE("AudioService: Initializing sndusb");
    }
#endif

    if (!m_pSound) {
        LOGERR("AudioService: Failed to create sound device");
        return FALSE;
    }

    return TRUE;
}

void CAudioService::EnsureAudioInitialized()
{
    // Signal the run loop to perform initialization safely in task context
    if (!m_bAudioInitialized) {
        m_bStartRequested = true;
    }
}

int CAudioService::Write(const void *pBuffer, unsigned nCount)
{
    if (!m_pSound) return -1;
    return m_pSound->Write(pBuffer, nCount);
}

boolean CAudioService::Start()
{
    if (!m_pSound) return FALSE;
    return m_pSound->Start();
}

void CAudioService::Stop()
{
    if (m_pSound) m_pSound->Cancel();
    m_bAudioInitialized = false;
    m_bStartRequested = false;
}

boolean CAudioService::IsActive() const
{
    if (!m_pSound) return FALSE;
    return m_pSound->IsActive();
}

unsigned CAudioService::GetQueueSizeFrames() const
{
    if (!m_pSound) return 0;
    return m_pSound->GetQueueSizeFrames();
}

unsigned CAudioService::GetQueueFramesAvail() const
{
    if (!m_pSound) return 0;
    return m_pSound->GetQueueFramesAvail();
}

void CAudioService::SetWriteFormat(TSoundFormat Format, unsigned nChannels)
{
    if (m_pSound) m_pSound->SetWriteFormat(Format, nChannels);
}

boolean CAudioService::AllocateQueueFrames(unsigned nFrames)
{
    if (!m_pSound) return FALSE;
    return m_pSound->AllocateQueueFrames(nFrames);
}

void CAudioService::SetVolume(u8 vol)
{
    m_Volume = vol;
}

void CAudioService::SetDefaultVolume(u8 vol)
{
    m_DefaultVolume = vol;
}

u8 CAudioService::GetVolume() const
{
    return m_Volume;
}

void CAudioService::ScaleVolume(u8 *buffer, u32 byteCount)
{
    if (m_Volume == 0xff && m_DefaultVolume == 0xff)
        return;

    // Convert both to Q12 scale
    u16 defaultScale = (m_DefaultVolume == 0xff) ? 4096 : (m_DefaultVolume << 4);
    u16 volumeScale  = (m_Volume == 0xff)         ? 4096 : (m_Volume << 4);

    // Combine both: result is Q12 * Q12 >> 12 = Q12 again
    u16 finalScale = (defaultScale * volumeScale) >> 12;

    for (u32 i = 0; i < byteCount; i += 2) {
        short sample = (short)((buffer[i + 1] << 8) | buffer[i]);
        int scaled = (sample * finalScale) >> 12;
        buffer[i] = (u8)(scaled & 0xFF);
        buffer[i + 1] = (u8)((scaled >> 8) & 0xFF);
    }
}

void CAudioService::Run(void)
{
    while (1) {
        // Handle deferred initialization request
        if (m_bStartRequested && !m_bAudioInitialized) {
            LOGNOTE("AudioService: Processing start request...");

            if (m_pSound) {
                if (m_pSound->IsActive()) {
                     m_bAudioInitialized = true;
                     m_bStartRequested = false;
                     LOGNOTE("AudioService: Audio already active.");
                } else {
                    if (m_pSound->Start()) {
                        m_bAudioInitialized = true;
                        m_bStartRequested = false;
                        LOGNOTE("AudioService: Audio initialized and started successfully.");
                    } else {
                        LOGERR("AudioService: Failed to start sound device.");
                        // Keep m_bStartRequested true? Or retry later?
                        // For now, let's reset it to prevent log spam, maybe CCDPlayer will ask again via EnsureAudioInitialized
                        m_bStartRequested = false;
                    }
                }
            } else {
                m_bStartRequested = false;
            }
        }

        CScheduler::Get()->MsSleep(100);
    }
}
