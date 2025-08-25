//
// A CD Player for USBODE
//
// Copyright (C) 2025 Ian Cass
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

CCDPlayer *CCDPlayer::s_pThis = nullptr;

CCDPlayer::CCDPlayer(const char *pSoundDevice)
    : m_pSoundDevice(pSoundDevice),
      //m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), TRUE) {
      m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), FALSE) {

    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    LOGNOTE("CD Player starting");
    SetName("cdplayer");
    Initialize();
}

boolean CCDPlayer::SetDevice(ICueDevice *pBinFileDevice) {
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
    LOGNOTE("CD Player Initializing. Allocating queue size %d frames", DAC_BUFFER_SIZE_FRAMES);
    if (!m_pSound->AllocateQueueFrames(DAC_BUFFER_SIZE_FRAMES)) {
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
    s_pThis = nullptr;
}

u8 CCDPlayer::GetVolume() {
    return volumeByte;
}

boolean CCDPlayer::SetDefaultVolume(u8 vol) {
    LOGNOTE("Setting default volume to 0x%02x", vol);
    defaultVolumeByte = vol;
    return true;
}

boolean CCDPlayer::SetVolume(u8 vol) {
    LOGNOTE("Setting volume to 0x%02x", vol);
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
    boolean success = false;

    // Read sound bytes and give them to the DAC
    for (unsigned nCount = 0; m_pSound->IsActive(); nCount++) {
        // Get available queue size in stereo frames
        unsigned int available_queue_size = total_frames - m_pSound->GetQueueFramesAvail();

        // Determine how many  frames (4 bytes) can fit in this free space
        int bytes_to_read = available_queue_size * BYTES_PER_FRAME;  // 2 bytes per sample, 2 samples per frame

        if (bytes_to_read) {
            if (f_read(&file, m_ReadBuffer, bytes_to_read, &bytesRead) != FR_OK) {
                LOGERR("Sound Test: Failed to read audio data");
                break;
            }

            if (bytesRead == 0) {
                LOGNOTE("Sound test: finished successfully");
                success = true;
                break;
            }

            int nResult = m_pSound->Write(m_ReadBuffer, bytesRead);
            if (nResult != (int)bytesRead) {
                LOGERR("Sound Test: data dropped");
                break;
            }
        }
        CScheduler::Get()->Yield();
    }
    f_close(&file);
    return success;
}

boolean CCDPlayer::Play(u32 lba, u32 num_blocks) {
    LOGNOTE("CD Player playing from %u for %u blocks", lba, num_blocks);

    address = lba;
    end_address = address + num_blocks;
    state = SEEKING_PLAYING;
    return true;
}

// DACs don't support volume control, so we scale the data
// accordingly instead
void CCDPlayer::ScaleVolume(u8 *buffer, u32 byteCount) {
    if (volumeByte == 0xff && defaultVolumeByte == 0xff)
        return;

    // Convert both to Q12 scale
    u16 defaultScale = (defaultVolumeByte == 0xff) ? 4096 : (defaultVolumeByte << 4);  // max = 0xff << 4 = 4080
    u16 volumeScale  = (volumeByte == 0xff)         ? 4096 : (volumeByte << 4);

    // Combine both: result is Q12 * Q12 >> 12 = Q12 again
    u16 finalScale = (defaultScale * volumeScale) >> 12;

    for (u32 i = 0; i < byteCount; i += 2) {
        short sample = (short)((buffer[i + 1] << 8) | buffer[i]);
        int scaled = (sample * finalScale) >> 12;
        buffer[i] = (u8)(scaled & 0xFF);
        buffer[i + 1] = (u8)((scaled >> 8) & 0xFF);
    }
}

void CCDPlayer::Run(void) {

    // Initialize buffer state variables.
    m_BufferBytesValid = 0;
    m_BufferReadPos = 0;
    m_BytesProcessedInSector = 0;
    unsigned int total_frames = m_pSound->GetQueueSizeFrames();
    m_WriteChunk = new u8[total_frames * BYTES_PER_FRAME];
    m_ReadBuffer = new u8[AUDIO_BUFFER_SIZE];

    LOGNOTE("CD Player Run Loop initializing. Queue Size is %d frames", total_frames);

    while (true) {
        if (state == SEEKING || state == SEEKING_PLAYING) {
            LOGNOTE("Seeking to sector %u (byte %u)", address, unsigned(address * SECTOR_SIZE));
            u64 offset = m_pBinFileDevice->Seek(unsigned(address * SECTOR_SIZE));

            // When we seek, the contents of our read buffer are now invalid.
            m_BufferBytesValid = 0;
            m_BufferReadPos = 0;
            m_BytesProcessedInSector = 0; // Reset byte progress on seek

            if (offset != (u64)(-1)) {
                LOGNOTE("Seeking successful");
                state = (state == SEEKING_PLAYING) ? PLAYING : STOPPED_OK;
            } else {
                LOGERR("Error seeking to byte position %u", address * SECTOR_SIZE);
                state = STOPPED_ERROR;
            }
        }

        if (state == PLAYING) {
            // Fill the read buffer if it has been consumed.
            if (m_BufferReadPos >= m_BufferBytesValid) {
                m_BufferReadPos = 0; // Reset read position

                u64 sectors_remaining = (address < end_address) ? (end_address - address) : 0;
                if (sectors_remaining == 0) {
                    LOGNOTE("Playback finished, no sectors remaining.");
                    state = STOPPED_OK;
                    m_BufferBytesValid = 0;
                    continue;
                }

                u64 sectors_to_read_now = (AUDIO_BUFFER_SIZE / SECTOR_SIZE < sectors_remaining) ? (AUDIO_BUFFER_SIZE / SECTOR_SIZE) : sectors_remaining;
                int bytes_to_read = sectors_to_read_now * SECTOR_SIZE;

		// Because another task could have moved the file pointer after a Yield(),
                // we MUST seek to our intended address before every read operation.
                u64 offset = m_pBinFileDevice->Seek(unsigned(address * SECTOR_SIZE));
                if (offset == (u64)(-1)) {
                    LOGERR("Pre-read seek failed at position %u", address * SECTOR_SIZE);
                    state = STOPPED_ERROR;
                    continue; // Re-evaluate state in the next loop iteration.
                }

                //LOGDBG("Buffer exhausted. Reading %d bytes from file.", bytes_to_read);
                int readCount = m_pBinFileDevice->Read(m_ReadBuffer, bytes_to_read);

                if (readCount < 0) {
                    LOGERR("File read error.");
                    state = STOPPED_ERROR;
                    m_BufferBytesValid = 0;
                } else {
                    if (readCount < bytes_to_read) {
                        LOGWARN("Partial read from file: Read %d, expected %d.", readCount, bytes_to_read);
                    }
                    m_BufferBytesValid = readCount;
                    if (m_BufferBytesValid == 0) {
                       LOGNOTE("Read 0 bytes, treating as end of track.");
                       state = STOPPED_OK;
                    }
                }
            }

            // Feed the sound device from our buffer, if we have valid data.
            if (m_BufferBytesValid > 0 && state == PLAYING) {
                unsigned int available_queue_size = total_frames - m_pSound->GetQueueFramesAvail();
                unsigned int bytes_for_sound_device = available_queue_size * BYTES_PER_FRAME;

                unsigned int bytes_available_in_buffer = m_BufferBytesValid - m_BufferReadPos;
                unsigned int bytes_to_process = (bytes_for_sound_device < bytes_available_in_buffer) ? bytes_for_sound_device : bytes_available_in_buffer;

                bytes_to_process -= (bytes_to_process % BYTES_PER_FRAME);

                if (bytes_to_process > 0) {
                    // copy from read buffer to write chunk.
                    for (unsigned int i = 0; i < bytes_to_process; ++i) {
                        m_WriteChunk[i] = m_ReadBuffer[m_BufferReadPos + i];
                    }

                    ScaleVolume(m_WriteChunk, bytes_to_process);

                    int writeCount = m_pSound->Write(m_WriteChunk, bytes_to_process);
                    if (writeCount < 0) {
                         LOGERR("Error writing to sound device.");
                         state = STOPPED_ERROR;
                    } else {
                        if ((unsigned int)writeCount != bytes_to_process) {
                            LOGWARN("Truncated write to sound device. Wrote %d, expected %d", writeCount, bytes_to_process);
                        }

                        m_BufferReadPos += writeCount;

                        m_BytesProcessedInSector += writeCount;
                        if (m_BytesProcessedInSector >= SECTOR_SIZE) {
                            address += m_BytesProcessedInSector / SECTOR_SIZE;
                            m_BytesProcessedInSector %= SECTOR_SIZE;
                        }

                        if (address >= end_address) {
                            LOGNOTE("Finished playing track range.");
                            state = STOPPED_OK;
                        }
                    }
                }
            }
        }
        CScheduler::Get()->Yield();
    }
}
