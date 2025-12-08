//
// A CD Player for USBODE
//
// Copyright (C) 2025 Ian Cass, Dani Sarfati
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
      m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), FALSE),
      m_pSound(nullptr),   
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

    // 1. STOP: Reset the physical Audio Driver to prevent DMA desync
    if (m_pSound && m_pSound->IsActive()) {
        m_pSound->Cancel();
        CScheduler::Get()->MsSleep(50); // Give hardware time to settle
    }

    // 2. RESET: Reset ALL address pointers
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
        memset(m_WriteChunk, 0, DAC_BUFFER_SIZE_BYTES);
    }
    
    m_pBinFileDevice = pBinFileDevice;
    
    // 3. START: Restart the Audio Driver (Resets internal DMA pointers to 0)
    if (m_pSound) {
        if (!m_pSound->Start()) {
            LOGERR("Failed to restart sound device after swap");
        }
    }
    
    LOGNOTE("CD Player device set complete: state=%u, device=%p", state, m_pBinFileDevice);
    return true;
}

// boolean CCDPlayer::Initialize() {
//     LOGNOTE("CD Player Initializing I2CMaster");
//     m_I2CMaster.Initialize();

//     if (strcmp(m_pSoundDevice, "sndpwm") == 0) {
//         m_pSound = new CPWMSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
//         LOGNOTE("CD Player Initializing sndpwm");
//     } else if (strcmp(m_pSoundDevice, "sndi2s") == 0) {
//         m_pSound = new CI2SSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE, FALSE,
//                                            &m_I2CMaster, DAC_I2C_ADDRESS);
//         LOGNOTE("CD Player Initializing sndi2c");
//     } else if (strcmp(m_pSoundDevice, "sndhdmi") == 0) {
//         m_pSound = new CHDMISoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
//         LOGNOTE("CD Player Initializing sndhdmi");
//     }
// #if RASPPI >= 4
//     else if (strcmp(m_pSoundDevice, "sndusb") == 0) {
//         m_pSound = new CUSBSoundBaseDevice(SAMPLE_RATE);
//         LOGNOTE("CD Player Initializing sndusb");
//     }
// #endif
//     /*
//             else
//             {
//      #ifdef USE_VCHIQ_SOUND
//                     m_pSound = new CVCHIQSoundBaseDevice (&m_VCHIQ, SAMPLE_RATE, SOUND_CHUNK_SIZE,
//                                             (TVCHIQSoundDestination) m_Options.GetSoundOption ());
//      #else
//                     m_pSound = new CPWMSoundBaseDevice (&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
//      #endif
//             }
//     */

//     // configure sound device
//     LOGNOTE("CD Player Initializing. Allocating queue size %d frames", DAC_BUFFER_SIZE_FRAMES);
//     if (!m_pSound->AllocateQueueFrames(DAC_BUFFER_SIZE_FRAMES)) {
//         LOGERR("Cannot allocate sound queue");
//         // TODO: handle error condition
//     }
//     m_pSound->SetWriteFormat(FORMAT, WRITE_CHANNELS);
//     if (!m_pSound->Start()) {
//         LOGERR("Couldn't start the sound device");
//     }

//     return TRUE;
// }

void CCDPlayer::EnsureAudioInitialized() {
    // CRITICAL FIX: Prevent double-initialization crash
    if (m_pSound != nullptr) {
        m_bAudioInitialized = true;
        return;
    }

    if (m_bAudioInitialized) {
        return;
    }
    
    LOGNOTE("=== LAZY I2S INITIALIZATION (after USB stabilization) ===");
    
    if (!m_I2CMaster.Initialize()) {
        LOGERR("Failed to initialize I2C master");
        return;
    }

    if (strcmp(m_pSoundDevice, "sndpwm") == 0) {
        m_pSound = new CPWMSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE);
        LOGNOTE("CD Player Initializing sndpwm");
    } else if (strcmp(m_pSoundDevice, "sndi2s") == 0) {
        m_pSound = new CI2SSoundBaseDevice(&m_Interrupt, SAMPLE_RATE, SOUND_CHUNK_SIZE, FALSE,
                                           &m_I2CMaster, DAC_I2C_ADDRESS);
        LOGNOTE("CD Player Initializing sndi2s");
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

    if (!m_pSound) {
        LOGERR("Failed to create sound device");
        return;
    }

    LOGNOTE("Allocating queue size %d frames", DAC_BUFFER_SIZE_FRAMES);
    if (!m_pSound->AllocateQueueFrames(DAC_BUFFER_SIZE_FRAMES)) {
        LOGERR("Cannot allocate sound queue");
        return;
    }
    
    m_pSound->SetWriteFormat(FORMAT, WRITE_CHANNELS);
    
    LOGNOTE("Default volume stored as: 0x%02x", defaultVolumeByte);
    
    if (!m_pSound->Start()) {
        LOGERR("Couldn't start the sound device");
        return;
    }
    
    m_bAudioInitialized = true;
    LOGNOTE("=== I2S INITIALIZED: active=%d ===", m_pSound->IsActive());
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
    // Only allow pause when currently playing
    if (state != PLAYING) {
        LOGNOTE("CD Player: Pause requested in invalid state (%u)", state);
        return false;
    }

    LOGNOTE("CD Player pausing");
    state = PAUSED;

    // STOP ENGINE: Prevent DMA underrun while paused
    if (m_pSound && m_pSound->IsActive()) {
        m_pSound->Cancel();
    }
    return true;
}

boolean CCDPlayer::Resume() {
    // Resume only valid from paused state
    if (state != PAUSED) {
        LOGNOTE("CD Player: Resume requested in invalid state (%u)", state);
        return false;
    }

    LOGNOTE("CD Player resuming");
    
    // RESTART ENGINE: Reset pointers and prepare for data
    if (m_pSound && !m_pSound->IsActive()) {
        m_pSound->Start();
    }
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
    // 1. Sanity Checks
    if (!m_pSound) {
        LOGERR("Sound Test: Can't perform test, sound is not active");
        return false;
    }
    // Note: We don't check IsActive() here because we will force a restart below

    // 2. Open File
    FIL file;
    FRESULT Result = f_open(&file, "system/test.pcm", FA_READ);
    if (Result != FR_OK) {
        LOGERR("Sound Test: Can't open test.pcm");
        return false;
    }

    // 3. Get File Size and Allocate RAM
    // f_size is a standard FatFS macro. If your version lacks it, use file.fsize
    unsigned int nTotalBytes = f_size(&file); 
    
    if (nTotalBytes == 0) {
        LOGERR("Sound Test: File is empty");
        f_close(&file);
        return false;
    }

    LOGNOTE("Sound Test: Loading %u bytes into RAM...", nTotalBytes);

    // Allocate aligned memory (new[] provides cache alignment on Circle)
    u8 *pWholeFileBuffer = new u8[nTotalBytes];
    if (!pWholeFileBuffer) {
        LOGERR("Sound Test: Out of memory! (Tried to alloc %u bytes)", nTotalBytes);
        f_close(&file);
        return false;
    }

    // 4. Read Entire File
    UINT bytesRead = 0;
    Result = f_read(&file, pWholeFileBuffer, nTotalBytes, &bytesRead);
    f_close(&file); // Close file immediately to free the bus

    if (Result != FR_OK || bytesRead != nTotalBytes) {
        LOGERR("Sound Test: Failed to read file (Read %d of %d)", bytesRead, nTotalBytes);
        delete[] pWholeFileBuffer;
        return false;
    }

    // RESET ENGINE: Ensure we are fresh before the loop starts
    if (m_pSound->IsActive()) m_pSound->Cancel();
    if (!m_pSound->Start()) {
        LOGERR("Sound Test: Failed to restart audio engine");
        delete[] pWholeFileBuffer;
        return false;
    }

    LOGNOTE("Sound Test: Load Complete. Starting RAM Playback...");

    // 5. Playback Loop
    unsigned int nOffset = 0;
    boolean success = true;

    while (nOffset < nTotalBytes && m_pSound->IsActive()) {
        // How much space is currently in the I2S DMA ring buffer?
        // Note: GetQueueFramesAvail returns FRAMES. Convert to BYTES.
        unsigned int nFramesAvail = m_pSound->GetQueueSizeFrames() - m_pSound->GetQueueFramesAvail();
        unsigned int nBytesAvail = nFramesAvail * BYTES_PER_FRAME;

        // How much do we have left to write?
        unsigned int nBytesRemaining = nTotalBytes - nOffset;

        // Determine write chunk size
        unsigned int nWriteSize = (nBytesRemaining < nBytesAvail) ? nBytesRemaining : nBytesAvail;

        if (nWriteSize > 0) {
            int nWritten = m_pSound->Write(&pWholeFileBuffer[nOffset], nWriteSize);

            if (nWritten != (int)nWriteSize) {
                LOGERR("Sound Test: Write failure or truncated! (Wrote %d expected %d)", nWritten, nWriteSize);
                success = false;
                break;
            }

            nOffset += nWritten;
        }

        // CRITICAL: Allow the scheduler to run other tasks (USB, Timers)
        // If we don't yield, the system freezes.
        CScheduler::Get()->Yield();
    }

    // STOP ENGINE: Prevent DMA desync after test completes
    if (m_pSound->IsActive()) m_pSound->Cancel();

    LOGNOTE("Sound Test: Playback Complete. Freeing RAM.");
    
    // 6. Cleanup
    delete[] pWholeFileBuffer;
    
    return success;
}

boolean CCDPlayer::Play(u32 lba, u32 num_blocks) {
    LOGNOTE("CD Player playing from %u for %u blocks (previous state=%u)", lba, num_blocks, state);

    // Validate media presence
    if (m_pBinFileDevice == nullptr) {
        LOGERR("CD Player: Play requested but no device set");
        return false;
    }

    // RESTART ENGINE: Ensure audio is running (e.g. if we stopped it in PlaybackStop)
    if (m_pSound && !m_pSound->IsActive()) {
        m_pSound->Start();
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

    // STOP ENGINE: Prevent DMA desync.
    // If we leave it running without data, it will loop/desync.
    if (m_pSound && m_pSound->IsActive()) {
        m_pSound->Cancel();
    }

    return true;
}

// Add to CCDPlayer class
boolean CCDPlayer::SineWaveTest() {
    if (!m_pSound) return false;

    LOGNOTE("Starting Audio Integrity Test (Heartbeat)...");
    
    // 1. Setup Constraints
    // We want 1 second of audio data
    // 44100 frames * 2 channels * 2 bytes (16-bit) = 176,400 bytes
    unsigned nFrames = 44100;
    unsigned nChannels = 2; // Stereo
    unsigned nBytes = nFrames * nChannels * sizeof(s16);

    // 2. Allocate a buffer as Signed 16-bit integers
    // Using 'new' ensures 64-byte alignment for DMA
    s16 *pBuffer = new s16[nFrames * nChannels]; 
    memset(pBuffer, 0, nBytes); // Start with silence

    // 3. Generate the "Beep" (Triangle Wave)
    // We will fill only the first 200ms (approx 8820 frames)
    unsigned nBeepFrames = 8820;
    
    // Pitch: 440Hz at 44.1kHz = period of ~100 frames
    int period = 100; 
    int half_period = 50;
    
    for (unsigned i = 0; i < nBeepFrames; i++) {
        // Simple triangle wave math: counts up then down
        int phase = i % period;
        int sampleVal;
        
        if (phase < half_period) {
            // Ramp Up: -10000 to +10000
            sampleVal = -10000 + (phase * 400); 
        } else {
            // Ramp Down: +10000 to -10000
            sampleVal = 10000 - ((phase - half_period) * 400);
        }

        // Write to LEFT channel (Even index)
        pBuffer[i * 2]     = (s16)sampleVal;
        
        // Write to RIGHT channel (Odd index)
        pBuffer[i * 2 + 1] = (s16)sampleVal;
    }

    // RESET ENGINE: Ensure we are fresh before playing
    if (m_pSound->IsActive()) m_pSound->Cancel();
    if (!m_pSound->Start()) {
        delete[] pBuffer;
        return false;
    }

    // 4. Play the loop 5 times
    // Pattern: [ BEEP (200ms) ....... SILENCE (800ms) ]
    for (int loop = 0; loop < 5; loop++) {
        
        // We cast back to u8* or void* for the Write function
        int written = m_pSound->Write(pBuffer, nBytes);
        
        if (written != (int)nBytes) {
            LOGERR("IntegrityTest: Buffer rejected! Wrote %d of %d", written, nBytes);
        }

        // Sleep for 1s to let the buffer drain naturally
        // This simulates the "Spacing" of the tasks without stressing the CPU
        CScheduler::Get()->Sleep(1); 
    }

    // STOP ENGINE: Prevent DMA desync
    if (m_pSound->IsActive()) m_pSound->Cancel();

    delete[] pBuffer;
    LOGNOTE("Integrity Test Complete");
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
    LOGNOTE("CD Player Run Loop started");
    
    boolean buffers_allocated = false;
    unsigned int total_frames = 0;

    while (true) {
        // STATE 1: Wait for audio initialization (triggered by USB endpoint activation)
        if (!m_bAudioInitialized) {
            CScheduler::Get()->Yield();
            continue;
        }
        
        // STATE 2: Audio initialized - allocate buffers once
        if (!buffers_allocated) {
            m_BufferBytesValid = 0;
            m_BufferReadPos = 0;
            m_BytesProcessedInSector = 0;
            total_frames = m_pSound->GetQueueSizeFrames();
            m_WriteChunk = new u8[total_frames * BYTES_PER_FRAME];
            m_ReadBuffer = new u8[AUDIO_BUFFER_SIZE];
            buffers_allocated = true;
            LOGNOTE("CD Player Run Loop initialized. Queue Size is %d frames", total_frames);
        }
        
        // STATE 3: Normal operation - seeking
        if (state == SEEKING || state == SEEKING_PLAYING) {
            LOGNOTE("Seeking to sector %u (byte %u)", address, unsigned(address * SECTOR_SIZE));
            u64 offset = m_pBinFileDevice->Seek(unsigned(address * SECTOR_SIZE));

            // When we seek, the contents of our read and write buffer are now invalid.
            memset(m_WriteChunk, 0, total_frames * BYTES_PER_FRAME);
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
                    
                    // STOP ENGINE: Prevent DMA desync when track finishes naturally
                    if (m_pSound && m_pSound->IsActive()) {
                        m_pSound->Cancel();
                    }

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
                       if (m_pSound && m_pSound->IsActive()) m_pSound->Cancel(); // Safety stop
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

                    //ScaleVolume(m_WriteChunk, bytes_to_process);
                    

                    int writeCount = m_pSound->Write(m_WriteChunk, bytes_to_process);
                    if (writeCount < 0) {
                         LOGERR("Error writing to sound device.");
                         state = STOPPED_ERROR;
                         // Error state - reset engine
                         if (m_pSound && m_pSound->IsActive()) m_pSound->Cancel();
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
                            if (m_pSound && m_pSound->IsActive()) m_pSound->Cancel();
                        }
                    }
                }
            }
        }
        CScheduler::Get()->Yield();
    }
}