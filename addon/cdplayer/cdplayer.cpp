//
// A Media player
//
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2020-2021  R. Stange <rsta2@o2online.de>
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
#include "cdplayer.h"

#include <assert.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>

LOGMODULE("cdplayer");

CCDPlayer *CCDPlayer::s_pThis = 0;

CCDPlayer::CCDPlayer(const char *pSoundDevice)
    : m_pSoundDevice(pSoundDevice),
      m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), TRUE) {
    assert(m_pSound != 0);

    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    LOGNOTE("CD Player starting");
    SetName("cdplayer");
    has_error = false;
    Initialize();
}

boolean CCDPlayer::SetDevice(CDevice *pBinFileDevice) {
    LOGNOTE("CD Player setting device");
    state = NONE;
    address = 0;
    m_pBinFileDevice = pBinFileDevice;
    return true;
}

boolean CCDPlayer::Initialize() {
    LOGNOTE("CD Player Initializing I2CMaster");
    m_I2CMaster.Initialize();

    if (strcmp(m_pSoundDevice, "sndpwm") == 0) {
        m_pSound = new CPWMSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
        LOGNOTE("CD Player Initializing sndpwm");
    } else if (strcmp(m_pSoundDevice, "sndi2s") == 0) {
        m_pSound = new CI2SSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE, FALSE,
                                           &m_I2CMaster, DAC_I2C_ADDRESS);
        LOGNOTE("CD Player Initializing sndi2c");
    } else if (strcmp(m_pSoundDevice, "sndhdmi") == 0) {
        m_pSound = new CHDMISoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
        LOGNOTE("CD Player Initializing sndhdmi");
    }
#if RASPPI >= 4
    else if (strcmp(m_pSoundDevice, "sndusb") == 0) {
        m_pSound = new CUSBSoundBaseDevice(SAMPLE_RATE);
        LOGNOTE("CD Player Initializing sndusb");
    }
#endif
    /*
            else
            {
     #ifdef USE_VCHIQ_SOUND
                    m_pSound = new CVCHIQSoundBaseDevice (&m_VCHIQ, SAMPLE_RATE, SOUND_CHUNK_SIZE,
                                            (TVCHIQSoundDestination) m_Options.GetSoundOption ());
     #else
                    m_pSound = new CPWMSoundBaseDevice (&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
     #endif
            }
    */

    // configure sound device
    LOGNOTE("CD Player Initializing. Allocating queue size %d frames", BUFFER_SIZE_FRAMES);
    if (!m_pSound->AllocateQueueFrames(BUFFER_SIZE_FRAMES)) {
        LOGERR("Cannot allocate sound queue");
        // TODO: handle error condition
    }
    m_pSound->SetWriteFormat(FORMAT, WRITE_CHANNELS);
    if (!m_pSound->Start()) {
        LOGERR("Couldn't start the sound device");
    }

    return TRUE;
}

CCDPlayer::~CCDPlayer(void) {
    s_pThis = 0;
}

u8 CCDPlayer::GetVolume() {
    return volumeByte;
}

boolean CCDPlayer::SetVolume(u8 vol) {
    volumeByte = vol;
    return true;
}

boolean CCDPlayer::Pause() {
    LOGNOTE("CD Player pausing");
    state = PAUSED;
    return true;
}

boolean CCDPlayer::Resume() {
    LOGNOTE("CD Player resuming");
    state = PLAYING;
    return true;
}

boolean CCDPlayer::Seek(u32 lba) {
    // See to the new lba
    LOGNOTE("CD Player seeking to %u", lba);
    address = lba;
    state = SEEKING;
    return true;
}

// READ SUB-CHANNEL response differentiates between
// a stopped with success, stopped with failure, and
// doing nothing. Stopped is a one time reported status
// and then we change to NONE
unsigned int CCDPlayer::GetState() {
    unsigned int s = state;

    if (state == STOPPED_ERROR || state == STOPPED_OK)
        state = NONE;
    return s;
}

u32 CCDPlayer::GetCurrentAddress() {
    return address;
}

// Loads a sample from "system/test.pcm" and plays it
// Returns false if there was any problem
boolean CCDPlayer::SoundTest() {
    if (!m_pSound->IsActive()) {
        LOGERR("Sound Test: Can't perform test, sound is not active");
        return false;
    }

    FIL file;
    FRESULT Result = f_open(&file, "system/test.pcm", FA_READ);
    if (Result != FR_OK) {
        LOGERR("Sound Test: Can't open test.pcm");
        return false;
    }

    unsigned int total_frames = m_pSound->GetQueueSizeFrames();
    UINT bytesRead = 0;

    // Read sound bytes and give them to the DAC
    for (unsigned nCount = 0; m_pSound->IsActive(); nCount++) {
        // Get available queue size in stereo frames
        unsigned int available_queue_size = total_frames - m_pSound->GetQueueFramesAvail();

        // Determine how many  frames (4 bytes) can fit in this free space
        int bytes_to_read = available_queue_size * BYTES_PER_FRAME;  // 2 bytes per sample, 2 samples per frame

        if (bytes_to_read) {
            if (f_read(&file, m_FileChunk, bytes_to_read, &bytesRead) != FR_OK) {
                LOGERR("Sound Test: Failed to read audio data");
                break;
            }

            if (bytesRead == 0) {
                LOGNOTE("Sound test: finished successfully");
                break;
            }

            int nResult = m_pSound->Write(m_FileChunk, bytesRead);
            if (nResult != (int)bytesRead) {
                LOGERR("Sound Test: data dropped");
                break;
            }
        }

        CScheduler::Get()->Yield();
    }
    f_close(&file);
    return false;
}

boolean CCDPlayer::Play(u32 lba, u32 num_blocks) {
    LOGNOTE("CD Player playing from %u for %u blocks", lba, num_blocks);
    // The PlayAudio SCSI command has some weird exceptions
    // for LBA addresses:-
    //
    // 00000000 do nothing. It's preferable that this method
    //          will not be called if an LBA of zero is
    //          encountered
    //
    // FFFFFFFF resume playing. It's preferable that the Resume
    //          method will be called instead of passing this
    //          value to this method

    if (lba == 0x00000000) {
        // do nothing
    } else if (lba == 0xFFFFFFFF) {
        // resume
        return this->Resume();
    } else {
        // play from new lba
        address = lba;
        end_address = address + num_blocks;
        state = SEEKING_PLAYING;
    }
    return true;
}

// DACs don't support volume control, so we scale the data
// accordingly instead
void CCDPlayer::ScaleVolume(u8 *buffer, u32 byteCount) {
    // Clamp and quantize volume to VOLUME_STEPS
    u32 index = (volumeByte * (VOLUME_STEPS - 1)) >> 8;  // 0–15
    u16 scale = s_VolumeTable[index];                    // fixed-point Q12

    // Scale each 16bit sample
    for (u32 i = 0; i < byteCount; i += 2) {
        // Load 16-bit signed little-endian sample
        short sample = (short)((buffer[i + 1] << 8) | buffer[i]);

        // Apply volume scaling
        int scaled = (sample * scale) >> VOLUME_SCALE_BITS;

        // Store back (little-endian)
        buffer[i] = (u8)(scaled & 0xFF);
        buffer[i + 1] = (u8)((scaled >> 8) & 0xFF);
    }
}

void CCDPlayer::Run(void) {
    unsigned int total_frames = m_pSound->GetQueueSizeFrames();
    LOGNOTE("CD Player Run Loop initializing. Queue Size is %d frames", total_frames);

    // Play loop
    while (true) {
        if (state == SEEKING || state == SEEKING_PLAYING) {
            LOGNOTE("Seeking to %u", unsigned(address * SECTOR_SIZE));
            u64 offset = m_pBinFileDevice->Seek(unsigned(address * SECTOR_SIZE));
            if (offset != (u64)(-1)) {
                LOGNOTE("Seeking successful");
                if (state == SEEKING_PLAYING) {
                    LOGNOTE("Switching to PLAYING mode");
                    state = PLAYING;
                } else {
                    state = STOPPED_OK;
                }
            } else {
                LOGERR("Error seeking to byte position %u", address * SECTOR_SIZE);
                has_error = true;
                state = STOPPED_ERROR;
                break;
            }
        }

        while (state == PLAYING) {
            // Get available queue size in stereo frames
            unsigned int available_queue_size = total_frames - m_pSound->GetQueueFramesAvail();

            // Determine how many  frames (4 bytes) can fit in this free space
            int bytes_to_read = available_queue_size * BYTES_PER_FRAME;  // 2 bytes per sample, 2 samples per frame

            // If we have queue space, fill it with some bytes
            if (bytes_to_read) {
                // LOGNOTE("Reading %u bytes", bytes_to_read);
                int readCount = m_pBinFileDevice->Read(m_FileChunk, bytes_to_read);
                // LOGDBG("Read %d bytes", readCount);

                // Partial read
                if (readCount < bytes_to_read) {
                    LOGERR("Partial read");
                    has_error = true;
                    state = STOPPED_ERROR;
                    break;
                }

                // Scale the volume
                if (volumeByte != 0xff)
                    ScaleVolume(m_FileChunk, readCount);

                // Write to sound device
                int writeCount = m_pSound->Write(m_FileChunk, readCount);
                if (writeCount != readCount) {
                    LOGERR("Truncated write, audio dropped");
                    has_error = true;
                    state = STOPPED_ERROR;
                    break;
                }

                // LOGNOTE("We are at %u", address);
                //  Keep track of where we are
                address += (readCount / SECTOR_SIZE);

                // Should we stop?
                if (address >= end_address) {
                    LOGNOTE("Finished playing");
                    state = STOPPED_OK;
                    break;
                }
            }

            // Let other tasks have cpu time
            CScheduler::Get()->Yield();
        }
        CScheduler::Get()->Yield();
    }
}
