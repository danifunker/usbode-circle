//
// cdplayer.h
//
// Base CD Audio Player Task for Circle
// Copyright (C) 2025 Ian Cass, Dani Sarfati
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
#ifndef _ccdplayer_h
#define _ccdplayer_h

#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/sched/task.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/usbsoundbasedevice.h>
#include <circle/new.h>
#include <circle/time.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>
#include <discimage/imagedevice.h>
#include <cueparser/cueparser.h>

#define SECTOR_SIZE 2352
#define BATCH_SIZE 16 
#define BYTES_PER_FRAME 4
#define FRAMES_PER_SECTOR (SECTOR_SIZE / BYTES_PER_FRAME)
#define DAC_BUFFER_SIZE_FRAMES (FRAMES_PER_SECTOR * BATCH_SIZE)
#define DAC_BUFFER_SIZE_BYTES (DAC_BUFFER_SIZE_FRAMES * BYTES_PER_FRAME)

#define SOUND_CHUNK_SIZE      (384 * 10)
#define SAMPLE_RATE 44100
#define WRITE_CHANNELS 2  // 1: Mono, 2: Stereo
#define FORMAT SoundFormatSigned16
#define DAC_I2C_ADDRESS 0

#define VOLUME_SCALE_BITS 12 // 1.0 = 4096
#define VOLUME_STEPS 16

#define AUDIO_BUFFER_SIZE  DAC_BUFFER_SIZE_FRAMES * BYTES_PER_FRAME

class CCDPlayer : public CTask {
   public:
    CCDPlayer(const char *pSoundDevice);
    ~CCDPlayer(void);
    boolean Initialize();
    void EnsureAudioInitialized();
    boolean SetDevice(IImageDevice *pBinFileDevice);
    boolean Pause();
    boolean Resume();
    boolean SetVolume(u8 vol);
    boolean SetDefaultVolume(u8 vol);
    u8 GetVolume();
    unsigned int GetState();
    boolean HadError();
    u32 GetCurrentAddress();
    boolean Seek(u32 lba);
    boolean Play(u32 lba, u32 num_blocks);
    boolean PlaybackStop();
    boolean SoundTest();
    size_t buffer_available();
    size_t buffer_free_space();
    void Run(void);

    enum PlayState {
        PLAYING,
        SEEKING,
        SEEKING_PLAYING,
        STOPPED_OK,
        STOPPED_ERROR,
        PAUSED,
	NONE
    };

   private:
    void ScaleVolume(u8 *buffer, u32 byteCount);

    // Map an LBA to its byte offset in the device, using the track that
    // contains it (sector sizes and file offsets are per-track).
    // Returns (u64)-1 if the LBA is not within any track.
    u64 LBAToByteOffset(u32 lba);

   private:
    const char *m_pSoundDevice;
    CI2CMaster m_I2CMaster;
    CInterruptSystem m_Interrupt;
    CSynchronizationEvent m_Event;
    static CCDPlayer *s_pThis;
    CSoundBaseDevice *m_pSound;
    IImageDevice *m_pBinFileDevice;
    u32 address;
    u32 end_address;
    PlayState state;
    u8 volumeByte = 255;
    u8 defaultVolumeByte = 255;
    boolean m_bAudioInitialized = false;  // NEW

    u8 *m_ReadBuffer;  // CHANGED: removed = new u8[AUDIO_BUFFER_SIZE]
    u8 *m_WriteChunk;
    unsigned int m_BufferBytesValid = 0;
    unsigned int m_BufferReadPos = 0;
    unsigned int m_BytesProcessedInSector = 0;

    // Track lookup state for LBAToByteOffset()
    CUEParser m_CueParser;
    CUETrackInfo m_CurrentTrack;
    u32 m_CurrentTrackEnd = 0;
    boolean m_HaveTrack = false;
};

#endif