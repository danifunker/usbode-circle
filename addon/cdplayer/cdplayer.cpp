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

CCDPlayer::CCDPlayer()
    : m_pAudioService(nullptr),
      m_pBinFileDevice(nullptr),
      address(0),
      end_address(0),
      state(NONE),
      m_ReadBuffer(nullptr),
      m_WriteChunk(nullptr) {

    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    LOGNOTE("CD Player starting");
    SetName("cdplayer");

    // Get the Audio Service
    m_pAudioService = (CAudioService *)CScheduler::Get()->GetTask(AUDIO_SERVICE_NAME);
    if (!m_pAudioService) {
        LOGERR("CD Player: Audio Service not found!");
    }
}

boolean CCDPlayer::SetDevice(IImageDevice *pBinFileDevice) {
    LOGNOTE("CD Player setting device (old=%p, new=%p, state=%u, addr=%u, end=%u)", 
            m_pBinFileDevice, pBinFileDevice, state, address, end_address);
    
    // CRITICAL: Stop any active playback before device swap
    if (state == PLAYING || state == PAUSED || state == SEEKING_PLAYING || state == SEEKING) {
        LOGWARN("Device swap during active playback (state=%u) - forcing stop", state);
        state = STOPPED_OK;
    } else {
        state = NONE;
    }
    
    // Reset ALL address pointers - critical for preventing reads to invalid LBAs
    address = 0;
    end_address = 0;
    
    // Clear all buffer state
    m_BufferBytesValid = 0;
    m_BufferReadPos = 0;
    m_BytesProcessedInSector = 0;
    
    // Zero out buffers to prevent stale data
    if (m_ReadBuffer) {
        memset(m_ReadBuffer, 0, AUDIO_BUFFER_SIZE);
    }
    if (m_WriteChunk) {
        // WriteChunk size depends on total_frames, but we can at least clear what we know
        memset(m_WriteChunk, 0, DAC_BUFFER_SIZE_BYTES);
    }
    
    m_pBinFileDevice = pBinFileDevice;
    
    LOGNOTE("CD Player device set complete: state=%u, device=%p", state, m_pBinFileDevice);
    return true;
}

boolean CCDPlayer::Initialize() {
    // Initialization is now handled by AudioService
    LOGNOTE("CD Player Initialize called (deferred to AudioService)");
    return TRUE;
}

CCDPlayer::~CCDPlayer(void) {
    s_pThis = nullptr;
}

u8 CCDPlayer::GetVolume() {
    if (m_pAudioService) return m_pAudioService->GetVolume();
    return 0;
}

boolean CCDPlayer::SetDefaultVolume(u8 vol) {
    if (m_pAudioService) {
        m_pAudioService->SetDefaultVolume(vol);
        return true;
    }
    return false;
}

boolean CCDPlayer::SetVolume(u8 vol) {
    if (m_pAudioService) {
        m_pAudioService->SetVolume(vol);
        return true;
    }
    return false;
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
    if (!m_pAudioService) {
        LOGERR("Sound Test: Can't perform test, audio service is missing");
        return false;
    }
    if (!m_pAudioService->IsActive()) {
        LOGERR("Sound Test: Can't perform test, sound is in use");
        return false;
    }

    FIL file;
    FRESULT Result = f_open(&file, "system/test.pcm", FA_READ);
    if (Result != FR_OK) {
        LOGERR("Sound Test: Can't open test.pcm");
        return false;
    }

    unsigned int total_frames = m_pAudioService->GetQueueSizeFrames();
    UINT bytesRead = 0;
    boolean success = false;

    // Read sound bytes and give them to the DAC
    for (unsigned nCount = 0; m_pAudioService->IsActive(); nCount++) {
        // Get available queue size in stereo frames
        unsigned int available_queue_size = total_frames - m_pAudioService->GetQueueFramesAvail();

        // Determine how many  frames (4 bytes) can fit in this free space
        int bytes_to_read = available_queue_size * AUDIO_BYTES_PER_FRAME;  // 2 bytes per sample, 2 samples per frame

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

            int nResult = m_pAudioService->Write(m_ReadBuffer, bytesRead);
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

    address = lba;
    end_address = address + num_blocks;
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


void CCDPlayer::Run(void) {
    LOGNOTE("CD Player Run Loop started");
    
    boolean buffers_allocated = false;
    unsigned int total_frames = 0;

    while (true) {
        if (!m_pAudioService) {
            CScheduler::Get()->Yield();
            continue;
        }

        // STATE 1: Wait for audio initialization
        m_pAudioService->EnsureAudioInitialized(); // This checks m_bAudioInitialized inside
        if (!m_pAudioService->IsActive()) { // Or check a flag if EnsureAudioInitialized is not enough
            // AudioService needs to be active
             CScheduler::Get()->Yield();
             continue;
        }
        
        // STATE 2: Audio initialized - allocate buffers once
        if (!buffers_allocated) {
            m_BufferBytesValid = 0;
            m_BufferReadPos = 0;
            m_BytesProcessedInSector = 0;
            total_frames = m_pAudioService->GetQueueSizeFrames();
            if (total_frames == 0) {
                 // Wait until queue size is valid
                 CScheduler::Get()->Yield();
                 continue;
            }
            m_WriteChunk = new u8[total_frames * AUDIO_BYTES_PER_FRAME];
            m_ReadBuffer = new u8[AUDIO_BUFFER_SIZE];
            buffers_allocated = true;
            LOGNOTE("CD Player Run Loop initialized. Queue Size is %d frames", total_frames);
        }
        
        // STATE 3: Normal operation - seeking
        if (state == SEEKING || state == SEEKING_PLAYING) {
            LOGNOTE("Seeking to sector %u (byte %u)", address, unsigned(address * SECTOR_SIZE));
            u64 offset = m_pBinFileDevice->Seek(unsigned(address * SECTOR_SIZE));

            // When we seek, the contents of our read and write buffer are now invalid.
            memset(m_WriteChunk, 0, total_frames * AUDIO_BYTES_PER_FRAME);
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

        // STATE 3: Normal operation - playing
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
                unsigned int available_queue_size = total_frames - m_pAudioService->GetQueueFramesAvail();
                unsigned int bytes_for_sound_device = available_queue_size * AUDIO_BYTES_PER_FRAME;

                unsigned int bytes_available_in_buffer = m_BufferBytesValid - m_BufferReadPos;
                unsigned int bytes_to_process = (bytes_for_sound_device < bytes_available_in_buffer) ? bytes_for_sound_device : bytes_available_in_buffer;

                bytes_to_process -= (bytes_to_process % AUDIO_BYTES_PER_FRAME);

                if (bytes_to_process > 0) {
                    // copy from read buffer to write chunk.
                    for (unsigned int i = 0; i < bytes_to_process; ++i) {
                        m_WriteChunk[i] = m_ReadBuffer[m_BufferReadPos + i];
                    }

                    m_pAudioService->ScaleVolume(m_WriteChunk, bytes_to_process);

                    int writeCount = m_pAudioService->Write(m_WriteChunk, bytes_to_process);
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
