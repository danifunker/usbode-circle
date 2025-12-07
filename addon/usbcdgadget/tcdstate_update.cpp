//
// tcdstate_update.cpp
//
// Update Loop for Data Transfer
//
#include <usbcdgadget/usbcdgadget.h>
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>
#include <circle/util.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (m_bDebugLogging)             \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

// this function is called periodically from task level for IO
//(IO must not be attempted in functions called from IRQ)
void CUSBCDGadget::Update()
{
    if (m_bPendingDiscSwap)
    {
        unsigned elapsed = CTimer::Get()->GetTicks() - m_nDiscSwapStartTick;
        MLOGNOTE("CUSBCDGadget::Update", "Pending disc swap: elapsed=%u, threshold=%u", 
                 elapsed, CLOCKHZ / 2000);
        
        if (elapsed >= CLOCKHZ / 2000)
        {
            m_bPendingDiscSwap = false;
            m_CDReady = true;
            m_mediaState = MediaState::MEDIUM_PRESENT_UNIT_ATTENTION;
            m_SenseParams.bSenseKey = 0x06;
            m_SenseParams.bAddlSenseCode = 0x28;
            m_SenseParams.bAddlSenseCodeQual = 0x00;
            bmCSWStatus = CD_CSW_STATUS_FAIL;
            discChanged = true;
            MLOGNOTE("CUSBCDGadget::Update",
                     "Disc swap complete: Transitioned to UNIT_ATTENTION after %u ticks", elapsed);
        }
    }

    switch (m_nState)
    {
    case TCDState::DataInRead:
    {
        u64 offset = 0;
        int readCount = 0;
        if (m_CDReady)
        {
            u32 max_lba = CDUtils::GetLeadoutLBA(this);
            if (m_nblock_address >= max_lba)
            {
                MLOGERR("UpdateRead", "Current LBA %u exceeds max %u - aborting transfer",
                        m_nblock_address, max_lba);
                setSenseData(0x05, 0x21, 0x00);
                sendCheckCondition();
                return;
            }

            if (m_nblock_address + m_nnumber_blocks > max_lba)
            {
                u32 old_count = m_nnumber_blocks;
                m_nnumber_blocks = max_lba - m_nblock_address;
                CDROM_DEBUG_LOG("UpdateRead", "Truncating remaining blocks from %u to %u",
                                old_count, m_nnumber_blocks);
            }

            offset = m_pDevice->Seek(block_size * m_nblock_address);

            if (offset != (u64)(-1))
            {
                size_t maxBlocks = m_IsFullSpeed ? MaxBlocksToReadFullSpeed : MaxBlocksToReadHighSpeed;
                size_t maxBufferSize = m_IsFullSpeed ? MaxInMessageSizeFullSpeed : MaxInMessageSize;

                u32 blocks_to_read_in_batch = m_nnumber_blocks;

                if (blocks_to_read_in_batch > maxBlocks)
                {
                    blocks_to_read_in_batch = maxBlocks;
                    m_nnumber_blocks -= maxBlocks;
                }
                else
                {
                    m_nnumber_blocks = 0;
                }

                u32 total_batch_size = blocks_to_read_in_batch * block_size;
                u32 total_transfer_size = blocks_to_read_in_batch * transfer_block_size;

                if (total_transfer_size > maxBufferSize)
                {
                    u32 safe_blocks = maxBufferSize / transfer_block_size;
                    blocks_to_read_in_batch = safe_blocks;
                    total_batch_size = blocks_to_read_in_batch * block_size;
                    total_transfer_size = blocks_to_read_in_batch * transfer_block_size;

                    if (m_nnumber_blocks > 0)
                    {
                        m_nnumber_blocks += (maxBlocks - blocks_to_read_in_batch);
                    }
                }

                if (total_batch_size > MaxInMessageSize)
                {
                    MLOGERR("UpdateRead", "BUFFER OVERFLOW: %u > %u",
                            total_batch_size, (u32)MaxInMessageSize);
                    blocks_to_read_in_batch = MaxInMessageSize / block_size;
                    total_batch_size = blocks_to_read_in_batch * block_size;
                    total_transfer_size = blocks_to_read_in_batch * transfer_block_size;
                    m_nnumber_blocks = 0;
                }

                readCount = m_pDevice->Read(m_FileChunk, total_batch_size);

                if (m_bDebugLogging)
                {
                    u32 start_lba = m_nblock_address;
                    u32 end_lba = m_nblock_address + blocks_to_read_in_batch;

                    u32 target_lbas[] = {0, 1, 16};

                    for (u32 target : target_lbas)
                    {
                        if (target >= start_lba && target < end_lba)
                        {
                            u32 relative_lba_index = target - start_lba;
                            u32 sector_start_offset = relative_lba_index * block_size;
                            u32 user_data_offset = sector_start_offset + skip_bytes;

                            if (user_data_offset + 64 <= (u32)readCount)
                            {
                                CDROM_DEBUG_LOG("UpdateRead", "=== RAW USER DATA for LBA %u (first 64 bytes) ===", target);
                                for (int i = 0; i < 64; i += 16)
                                {
                                    u8 *p = m_FileChunk + user_data_offset;
                                    CDROM_DEBUG_LOG("UpdateRead", "[%04x] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                                                    i,
                                                    p[i + 0], p[i + 1], p[i + 2], p[i + 3],
                                                    p[i + 4], p[i + 5], p[i + 6], p[i + 7],
                                                    p[i + 8], p[i + 9], p[i + 10], p[i + 11],
                                                    p[i + 12], p[i + 13], p[i + 14], p[i + 15]);
                                }
                            }
                        }
                    }
                }

                if (readCount <= 0)
                {
                    MLOGERR("UpdateRead", "Read failed: returned %d bytes (expected %u) at LBA %u",
                            readCount, total_batch_size, m_nblock_address - blocks_to_read_in_batch);

                    if (readCount == 0)
                    {
                        setSenseData(0x05, 0x21, 0x00);
                    }
                    else
                    {
                        setSenseData(0x03, 0x11, 0x00);
                    }

                    sendCheckCondition();
                    return;
                }

                if (readCount < static_cast<int>(total_batch_size))
                {
                    MLOGERR("UpdateRead", "Partial read: %d/%u bytes at LBA %u",
                            readCount, total_batch_size, m_nblock_address - blocks_to_read_in_batch);

                    setSenseData(0x03, 0x11, 0x00);
                    sendCheckCondition();
                    return;
                }

                u8 *dest_ptr = m_InBuffer;
                u32 total_copied = 0;

                if (transfer_block_size == block_size && skip_bytes == 0)
                {
                    memcpy(dest_ptr, m_FileChunk, total_transfer_size);
                    total_copied = total_transfer_size;
                }
                else if (transfer_block_size > block_size)
                {
                    for (u32 i = 0; i < blocks_to_read_in_batch; ++i)
                    {
                        u8 sector2352[2352] = {0};
                        int offset = 0;

                        if (mcs & 0x10)
                        {
                            sector2352[0] = 0x00;
                            memset(&sector2352[1], 0xFF, 10);
                            sector2352[11] = 0x00;
                            offset = 12;
                        }

                        if (mcs & 0x08)
                        {
                            u32 lba = m_nblock_address + i + 150;
                            sector2352[offset++] = lba / (75 * 60);
                            sector2352[offset++] = (lba / 75) % 60;
                            sector2352[offset++] = lba % 75;
                            sector2352[offset++] = 0x01;
                        }

                        if (mcs & 0x04)
                        {
                            memcpy(&sector2352[offset], m_FileChunk + (i * block_size), 2048);
                            offset += 2048;
                        }

                        if (mcs & 0x02)
                        {
                            offset += 288;
                        }

                        memcpy(dest_ptr, sector2352 + skip_bytes, transfer_block_size);
                        dest_ptr += transfer_block_size;
                        total_copied += transfer_block_size;
                    }
                }
                else
                {
                    for (u32 i = 0; i < blocks_to_read_in_batch; ++i)
                    {
                        memcpy(dest_ptr, m_FileChunk + (i * block_size) + skip_bytes,
                               transfer_block_size);
                        dest_ptr += transfer_block_size;
                        total_copied += transfer_block_size;
                    }
                }

                m_nblock_address += blocks_to_read_in_batch;
                m_nbyteCount -= total_copied;
                m_nState = TCDState::DataIn;

                if (m_bDebugLogging)
                {
                    u32 start_lba = m_nblock_address - blocks_to_read_in_batch;
                    if (start_lba == 0 || start_lba == 1)
                    {
                        CDROM_DEBUG_LOG("UpdateRead", "=== LBA %u DATA DUMP (first 128 bytes) ===", start_lba);
                        for (int i = 0; i < 128 && i < (int)total_copied; i += 16)
                        {
                            CDROM_DEBUG_LOG("UpdateRead", "[%04x] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                                            i,
                                            m_InBuffer[i], m_InBuffer[i + 1], m_InBuffer[i + 2], m_InBuffer[i + 3],
                                            m_InBuffer[i + 4], m_InBuffer[i + 5], m_InBuffer[i + 6], m_InBuffer[i + 7],
                                            m_InBuffer[i + 8], m_InBuffer[i + 9], m_InBuffer[i + 10], m_InBuffer[i + 11],
                                            m_InBuffer[i + 12], m_InBuffer[i + 13], m_InBuffer[i + 14], m_InBuffer[i + 15]);
                        }
                    }
                }

                uintptr_t buffer_start = (uintptr_t)m_InBuffer;
                uintptr_t buffer_end = buffer_start + total_copied;

                buffer_start &= ~63UL;
                buffer_end = (buffer_end + 63) & ~63UL;

                for (uintptr_t addr = buffer_start; addr < buffer_end; addr += 64)
                {
#if AARCH == 64
                    asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
#else
                    asm volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(addr) : "memory");
#endif
                }

                DataSyncBarrier();

                CDROM_DEBUG_LOG("UpdateRead", "Transferred %u bytes, next_LBA=%u, remaining=%u",
                                total_copied, m_nblock_address, m_nnumber_blocks);

                if (mcs != 0 && m_pDevice->HasSubchannelData())
                {
                    u8 subChannelSelection = mcs & 0x07;

                    if (subChannelSelection != 0)
                    {
                        CDROM_DEBUG_LOG("UpdateRead", "Appending subchannel data for %u blocks",
                                        blocks_to_read_in_batch);

                        for (u32 i = 0; i < blocks_to_read_in_batch; i++)
                        {
                            u32 current_lba = m_nblock_address - blocks_to_read_in_batch + i;
                            u8 subchannel_buf[96];

                            int sc_result = m_pDevice->ReadSubchannel(current_lba, subchannel_buf);

                            if (sc_result == 96)
                            {
                                u32 offset = total_copied + (i * 96);
                                memcpy(m_InBuffer + offset, subchannel_buf, 96);
                                total_copied += 96;
                            }
                            else
                            {
                                CDROM_DEBUG_LOG("UpdateRead", "Subchannel read failed for LBA %u, zero-filling",
                                                current_lba);
                                u32 offset = total_copied + (i * 96);
                                memset(m_InBuffer + offset, 0, 96);
                                total_copied += 96;
                            }
                        }
                    }
                }

                m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                           m_InBuffer, total_copied);
            }
        }

        if (!m_CDReady || offset == (u64)(-1))
        {
            MLOGERR("UpdateRead", "Failed: ready=%d, offset=%llu", m_CDReady, offset);
            setSenseData(0x02, 0x04, 0x00);
            sendCheckCondition();
        }
        break;
    }
    default:
        break;
    }
}