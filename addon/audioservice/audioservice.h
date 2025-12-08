#ifndef _audioservice_h
#define _audioservice_h

#include <circle/sched/task.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/usbsoundbasedevice.h>
#include <circle/i2cmaster.h>
#include <circle/interrupt.h>
#include <circle/logger.h>

#define AUDIO_SERVICE_NAME "audioservice"

// Audio Constants
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_FORMAT SoundFormatSigned16
#define AUDIO_BYTES_PER_FRAME 4 // 16-bit stereo = 2 * 2 bytes
#define AUDIO_CHUNK_SIZE (384 * 10)

class CAudioService : public CTask
{
public:
    CAudioService(const char *pSoundDevice, CInterruptSystem *pInterrupt);
    ~CAudioService();

    boolean Initialize();
    void EnsureAudioInitialized();

    int Write(const void *pBuffer, unsigned nCount);
    boolean Start();
    boolean IsActive() const;
    unsigned GetQueueSizeFrames() const;
    unsigned GetQueueFramesAvail() const;
    void SetWriteFormat(TSoundFormat Format, unsigned nChannels);
    boolean AllocateQueueFrames(unsigned nFrames);

    // Volume control
    void SetVolume(u8 vol);
    void SetDefaultVolume(u8 vol);
    u8 GetVolume() const;
    void ScaleVolume(u8 *buffer, u32 byteCount);

    void Run(void);

private:
    const char *m_pSoundDeviceName;
    CI2CMaster *m_pI2CMaster;
    CInterruptSystem *m_pInterrupt;
    CSoundBaseDevice *m_pSound;

    u8 m_Volume;
    u8 m_DefaultVolume;
    boolean m_bAudioInitialized;
    volatile boolean m_bStartRequested;
};

#endif
