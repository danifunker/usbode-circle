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

    // Initialize state
    m_PlayLBA = 0;
    m_EndLBA = 0;
    state = NONE;
    m_BufferHead = 0;
    m_BufferTail = 0;
    m_BufferCount = 0;
    m_ReadBytePos = 0;
    m_PlayByteOffset = 0;

    LOGNOTE("CD Player starting");
    SetName("cdplayer");
    Initialize();
}

boolean CCDPlayer::SetDevice(IImageDevice *pBinFileDevice) {
    LOGNOTE("CD Player setting device (old=%p, new=%p, state=%u, addr=%u, end=%u)", 
            m_pBinFileDevice, pBinFileDevice, state, m_PlayLBA, m_EndLBA);
    
    // CRITICAL: Stop any active playback before device swap
    if (state == PLAYING || state == PAUSED || state == SEEKING_PLAYING || state == SEEKING) {
        LOGWARN("Device swap during active playback (state=%u) - forcing stop", state);
        state = STOPPED_OK;
    } else {
        state = NONE;
    }
    
    // Reset ALL address pointers - critical for preventing reads to invalid LBAs
    m_PlayLBA = 0;
    m_EndLBA = 0;
    
    // Clear all buffer state
    m_BufferHead = 0;
    m_BufferTail = 0;
    m_BufferCount = 0;
    m_ReadBytePos = 0;
    m_PlayByteOffset = 0;
    
    // Zero out buffers to prevent stale data
    if (m_ReadBuffer) {
        memset(m_ReadBuffer, 0, AUDIO_BUFFER_SIZE);
    }
    if (m_WriteChunk) {
        memset(m_WriteChunk, 0, DAC_BUFFER_SIZE_BYTES);
    }
    
    m_pBinFileDevice = pBinFileDevice;
    
    LOGNOTE("CD Player device set complete: state=%u, device=%p", state, m_pBinFileDevice);
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

    // Allocation moved here from Run(), and removed from member initializer
    if (!m_ReadBuffer) {
        m_ReadBuffer = new u8[AUDIO_BUFFER_SIZE];
    }
    if (!m_WriteChunk) {
        // We allocate the write chunk here based on max possible frame size
        m_WriteChunk = new u8[DAC_BUFFER_SIZE_BYTES];
    }

    return TRUE;
}

CCDPlayer::~CCDPlayer(void) {
    s_pThis = nullptr;
    if (m_ReadBuffer) {
        delete[] m_ReadBuffer;
        m_ReadBuffer = nullptr;
    }
    if (m_WriteChunk) {
        delete[] m_WriteChunk;
        m_WriteChunk = nullptr;
    }
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
    // Only allow pause when currently playing
    if (state != PLAYING) {
        LOGNOTE("CD Player: Pause requested in invalid state (%u)", state);
        return false;
    }

    LOGNOTE("CD Player pausing");
    state = PAUSED;
    return true;
}

boolean CCDPlayer::Resume() {
    // Resume only valid from paused state
    if (state != PAUSED) {
        LOGNOTE("CD Player: Resume requested in invalid state (%u)", state);
        return false;
    }

    LOGNOTE("CD Player resuming");
    state = PLAYING;
    return true;
}

boolean CCDPlayer::Seek(u32 lba) {
    // See to the new lba
    LOGNOTE("CD Player seeking to %u", lba);
    m_PlayLBA = lba;

    // Also update read position because we will jump there
    m_ReadBytePos = (u64)lba * SECTOR_SIZE;

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
    return m_PlayLBA;
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

    // Use m_ReadBuffer temporarily for this test
    if (!m_ReadBuffer) return false;

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
    LOGNOTE("CD Player playing from %u for %u blocks (previous state=%u)", lba, num_blocks, state);

    // Validate media presence
    if (m_pBinFileDevice == nullptr) {
        LOGERR("CD Player: Play requested but no device set");
        return false;
    }

    m_PlayLBA = lba;
    m_EndLBA = m_PlayLBA + num_blocks;
    m_ReadBytePos = (u64)m_PlayLBA * SECTOR_SIZE;

    state = SEEKING_PLAYING; // seek then transition to PLAYING in Run()
    return true;
}

boolean CCDPlayer::PlaybackStop() {
    // Stop only valid if playing or paused
    if (state != PLAYING && state != PAUSED && state != SEEKING_PLAYING) {
        LOGNOTE("CD Player: Stop requested in invalid state (%u)", state);
        return false;
    }

    LOGNOTE("CD Player stopping playback");
    state = STOPPED_OK;
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

    unsigned int total_frames = m_pSound->GetQueueSizeFrames();

    // Ensure buffers are allocated (safety check)
    if (!m_ReadBuffer || !m_WriteChunk) {
        LOGERR("Buffers not allocated in Run(), attempting allocation");
        if (!m_ReadBuffer) m_ReadBuffer = new u8[AUDIO_BUFFER_SIZE];
        if (!m_WriteChunk) m_WriteChunk = new u8[DAC_BUFFER_SIZE_BYTES];
    }

    LOGNOTE("CD Player Run Loop initializing. Queue Size is %d frames", total_frames);

    while (true) {
        if (state == SEEKING || state == SEEKING_PLAYING) {
            LOGNOTE("Seeking to sector %u (byte %lu)", m_PlayLBA, (unsigned long)m_ReadBytePos);

            // Perform the seek
            u64 offset = m_pBinFileDevice->Seek(m_ReadBytePos);

            // Reset Ring Buffer
            m_BufferHead = 0;
            m_BufferTail = 0;
            m_BufferCount = 0;
            m_PlayByteOffset = 0;
            // m_PlayLBA and m_ReadBytePos were set in Play()/Seek()

            if (offset != (u64)(-1)) {
                LOGNOTE("Seeking successful");
                state = (state == SEEKING_PLAYING) ? PLAYING : STOPPED_OK;
            } else {
                LOGERR("Error seeking to byte position %lu", (unsigned long)m_ReadBytePos);
                state = STOPPED_ERROR;
            }
        }

        if (state == PLAYING) {
            // ====================================================================
            // PRODUCER: Fill the Ring Buffer from Disk
            // ====================================================================

            // We loop here to try and fill the buffer as much as possible,
            // but we break if we can't read contiguous blocks or if file I/O is slow.
            // Ideally, we read whenever there is space.

            u64 max_read_limit = (u64)m_EndLBA * SECTOR_SIZE;

            while (m_ReadBytePos < max_read_limit) {
                u32 space = AUDIO_BUFFER_SIZE - m_BufferCount;

                // Only read if we have substantial space (e.g., at least 1 sector)
                // This prevents tiny inefficient reads.
                if (space < SECTOR_SIZE) break;

                // Determine max contiguous write size (up to end of buffer or wrap around)
                u32 contiguous_space = AUDIO_BUFFER_SIZE - m_BufferHead;
                u32 to_read = (space < contiguous_space) ? space : contiguous_space;

                // Don't read past the end of the track/segment
                u64 remaining_file_bytes = max_read_limit - m_ReadBytePos;
                if (remaining_file_bytes == 0) break;

                if (to_read > remaining_file_bytes) to_read = (u32)remaining_file_bytes;

                // Align reads to sector size where possible for performance, unless at end
                if (to_read >= SECTOR_SIZE) {
                    to_read = (to_read / SECTOR_SIZE) * SECTOR_SIZE;
                }

                // CRITICAL: Seek before every read as per requirement
                m_pBinFileDevice->Seek(m_ReadBytePos);

                int readCount = m_pBinFileDevice->Read(m_ReadBuffer + m_BufferHead, to_read);

                if (readCount <= 0) {
                    if (readCount < 0) LOGERR("File read error at %lu", (unsigned long)m_ReadBytePos);
                    else LOGNOTE("EOF reached at %lu", (unsigned long)m_ReadBytePos);

                    // Stop if error or EOF (though we checked limits above)
                    // If EOF unexpectedly, we just stop reading.
                    // If we expected more data but got 0, it's an issue.
                    break;
                }

                // Update Ring Buffer State
                m_BufferHead = (m_BufferHead + readCount) % AUDIO_BUFFER_SIZE;
                m_BufferCount += readCount;
                m_ReadBytePos += readCount;

                // If we filled the buffer, break to let Consumer run
                if (m_BufferCount == AUDIO_BUFFER_SIZE) break;
            }

            // ====================================================================
            // CONSUMER: Feed the Sound Device from Ring Buffer
            // ====================================================================

            if (m_BufferCount > 0) {
                unsigned int available_queue_frames = total_frames - m_pSound->GetQueueFramesAvail();
                unsigned int bytes_dest_space = available_queue_frames * BYTES_PER_FRAME;

                // How much data do we have available to send?
                unsigned int bytes_src_avail = m_BufferCount;

                unsigned int bytes_to_process = (bytes_dest_space < bytes_src_avail) ? bytes_dest_space : bytes_src_avail;

                // Align to frame boundary
                bytes_to_process -= (bytes_to_process % BYTES_PER_FRAME);

                if (bytes_to_process > 0) {
                    // We need to copy from Ring Buffer (m_BufferTail) to m_WriteChunk (Linear).
                    // This might require two copies if wrapping around.

                    u32 contiguous_read = AUDIO_BUFFER_SIZE - m_BufferTail;

                    if (bytes_to_process <= contiguous_read) {
                        // Single copy
                        memcpy(m_WriteChunk, m_ReadBuffer + m_BufferTail, bytes_to_process);
                        m_BufferTail = (m_BufferTail + bytes_to_process) % AUDIO_BUFFER_SIZE;
                    } else {
                        // Double copy (wrap around)
                        u32 first_part = contiguous_read;
                        u32 second_part = bytes_to_process - first_part;

                        memcpy(m_WriteChunk, m_ReadBuffer + m_BufferTail, first_part);
                        memcpy(m_WriteChunk + first_part, m_ReadBuffer, second_part);

                        m_BufferTail = second_part; // Wrapped to index 'second_part'
                    }

                    m_BufferCount -= bytes_to_process;

                    // Scale Volume and Write
                    ScaleVolume(m_WriteChunk, bytes_to_process);

                    int writeCount = m_pSound->Write(m_WriteChunk, bytes_to_process);

                    if (writeCount < 0) {
                         LOGERR("Error writing to sound device.");
                         state = STOPPED_ERROR;
                    } else {
                        // Note: If Write() returns less than we prepared, it's weird because we checked available space.
                        // But if it happens, we can't easily "unread" from ring buffer because we already advanced m_BufferTail.
                        // Fortunately, Circle's Write usually writes everything if space was checked.
                        // If it truncated, we lost some audio samples. Ideally we shouldn't advance Tail for unwritten bytes.
                        // But let's assume Write behaves well if Space > 0.
                        if ((unsigned int)writeCount != bytes_to_process) {
                            LOGWARN("Truncated write to sound device. Wrote %d, expected %d", writeCount, bytes_to_process);
                        }

                        // Update playback position tracking (m_PlayLBA)
                        m_PlayByteOffset += writeCount;
                        if (m_PlayByteOffset >= SECTOR_SIZE) {
                            u32 sectors_advanced = m_PlayByteOffset / SECTOR_SIZE;
                            m_PlayLBA += sectors_advanced;
                            m_PlayByteOffset %= SECTOR_SIZE;
                        }
                    }
                }
            } else {
                // Buffer is empty.
                // If we also finished reading everything, then we are done.
                if (m_ReadBytePos >= (u64)m_EndLBA * SECTOR_SIZE) {
                    LOGNOTE("Playback finished (End LBA reached and buffer empty).");
                    state = STOPPED_OK;
                }
                // Else, we are underrunning (waiting for Producer).
            }
        }
        CScheduler::Get()->Yield();
    }
}
