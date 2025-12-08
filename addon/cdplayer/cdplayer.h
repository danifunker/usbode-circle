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
#include <circle/sched/synchronizationevent.h>
#include <circle/sched/task.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/types.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <linux/kernel.h>
#include <discimage/imagedevice.h>
#include <audioservice/audioservice.h>

#define SECTOR_SIZE 2352
#define BATCH_SIZE 16 
#define BYTES_PER_FRAME 4
#define FRAMES_PER_SECTOR (SECTOR_SIZE / BYTES_PER_FRAME)
#define DAC_BUFFER_SIZE_FRAMES (FRAMES_PER_SECTOR * BATCH_SIZE)
#define DAC_BUFFER_SIZE_BYTES (DAC_BUFFER_SIZE_FRAMES * BYTES_PER_FRAME)

#define AUDIO_BUFFER_SIZE  DAC_BUFFER_SIZE_FRAMES * BYTES_PER_FRAME

class CCDPlayer : public CTask {
   public:
    CCDPlayer(void);
    ~CCDPlayer(void);
    boolean Initialize();
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
    
   private:
    CAudioService *m_pAudioService;
    CSoundBaseDevice *m_pSound;
    IImageDevice *m_pBinFileDevice;
    u32 address;
    u32 end_address;
    PlayState state;
    u8 volumeByte = 255;
    u8 defaultVolumeByte = 255;

    u8 *m_ReadBuffer;
    u8 *m_WriteChunk;
    unsigned int m_BufferBytesValid = 0;
    unsigned int m_BufferReadPos = 0;
    unsigned int m_BytesProcessedInSector = 0;
};

#endif
