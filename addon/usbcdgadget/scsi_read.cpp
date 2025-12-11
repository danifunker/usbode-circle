//
// scsi_read.cpp
//
// SCSI Read, Play Audio, Seek, Pause/Resume, Stop/Scan
//
#include <usbcdgadget/scsi_read.h>
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <cdplayer/cdplayer.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

void SCSIRead::Read10(CUSBCDGadget* gadget)
{
    DoRead(gadget, 10);
}

void SCSIRead::Read12(CUSBCDGadget* gadget)
{
    DoRead(gadget, 12);
}

void SCSIRead::PlayAudio10(CUSBCDGadget* gadget)
{
    DoPlayAudio(gadget, 10);
}

void SCSIRead::PlayAudio12(CUSBCDGadget* gadget)
{
    DoPlayAudio(gadget, 12);
}

void SCSIRead::DoRead(CUSBCDGadget* gadget, int cdbSize)
{
    if (gadget->m_CDReady)
    {
        if (cdbSize == 12)
        {
            // Where to start reading (LBA) - 4 bytes
            gadget->m_nblock_address = (u32)(gadget->m_CBW.CBWCB[2] << 24) | (u32)(gadget->m_CBW.CBWCB[3] << 16) |
                               (u32)(gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

            // Number of blocks to read (LBA) - 4 bytes
            gadget->m_nnumber_blocks = (u32)(gadget->m_CBW.CBWCB[6] << 24) | (u32)(gadget->m_CBW.CBWCB[7] << 16) |
                               (u32)(gadget->m_CBW.CBWCB[8] << 8) | gadget->m_CBW.CBWCB[9];

            gadget->m_nbyteCount = gadget->m_CBW.dCBWDataTransferLength;

            // What is this?
            if (gadget->m_nnumber_blocks == 0)
            {
                gadget->m_nnumber_blocks = 1 + (gadget->m_nbyteCount) / 2048;
            }
        }
        else
        {
            gadget->m_nbyteCount = gadget->m_CBW.dCBWDataTransferLength;

            // Where to start reading (LBA)
            gadget->m_nblock_address = (u32)(gadget->m_CBW.CBWCB[2] << 24) | (u32)(gadget->m_CBW.CBWCB[3] << 16) |
                               (u32)(gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

            // Number of blocks to read (LBA)
            gadget->m_nnumber_blocks = (u32)((gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8]);
        }

        // Get disc boundaries
        u32 max_lba = CDUtils::GetLeadoutLBA(gadget);

        // CRITICAL: Validate LBA is within disc boundaries
        if (gadget->m_nblock_address >= max_lba)
        {
            MLOGERR("SCSIRead::DoRead", "LBA %u beyond disc boundary (max=%u)",
                    gadget->m_nblock_address, max_lba);

            // SCSI error: ILLEGAL REQUEST / LOGICAL BLOCK ADDRESS OUT OF RANGE
            gadget->setSenseData(0x05, 0x21, 0x00);
            gadget->sendCheckCondition();
            return;
        }

        // Check if read extends beyond disc boundary
        if (gadget->m_nblock_address + gadget->m_nnumber_blocks > max_lba)
        {
            u32 original_blocks = gadget->m_nnumber_blocks;
            gadget->m_nnumber_blocks = max_lba - gadget->m_nblock_address;

            MLOGNOTE("SCSIRead::DoRead", "Read truncated: LBA=%u, requested=%u, max=%u, truncated to=%u",
                     gadget->m_nblock_address, original_blocks, max_lba, gadget->m_nnumber_blocks);
        }

        // Validate we have blocks to read after boundary checks
        if (gadget->m_nnumber_blocks == 0)
        {
            MLOGERR("SCSIRead::DoRead", "No blocks to read after boundary check");
            gadget->setSenseData(0x05, 0x21, 0x00);
            gadget->sendCheckCondition();
            return;
        }

        CDROM_DEBUG_LOG("SCSIRead::DoRead", "LBA=%u, cnt=%u, max_lba=%u",
                        gadget->m_nblock_address, gadget->m_nnumber_blocks, max_lba);

        // Transfer Block Size is the size of data to return to host
        // Block Size and Skip Bytes is worked out from cue sheet
        // For a CDROM, this is always 2048
        gadget->transfer_block_size = 2048;
        gadget->block_size = gadget->data_block_size; // set at SetDevice
        gadget->skip_bytes = gadget->data_skip_bytes; // set at SetDevice
        gadget->mcs = 0;
        gadget->m_nbyteCount = gadget->m_CBW.dCBWDataTransferLength;

        // Recalculate byte count based on potentially truncated block count
        u32 expected_byte_count = gadget->m_nnumber_blocks * gadget->transfer_block_size;
        if (gadget->m_nbyteCount > expected_byte_count)
        {
            MLOGNOTE("SCSIRead::DoRead", "Host requested %u bytes but only %u available",
                     gadget->m_nbyteCount, expected_byte_count);
            gadget->m_nbyteCount = expected_byte_count;
        }

        gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
        
        // Set transfer mode for simple reads (always SIMPLE_COPY, no subchannel)
        gadget->m_TransferMode = CUSBCDGadget::TransferMode::SIMPLE_COPY;
        gadget->m_NeedsSubchannel = false;
        
        gadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
    }
    else
    {
        CDROM_DEBUG_LOG("SCSIRead::DoRead", "failed, %s",
                        gadget->m_CDReady ? "ready" : "not ready");
        gadget->setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
    }
}

void SCSIRead::DoPlayAudio(CUSBCDGadget* gadget, int cdbSize)
{
    MLOGNOTE("SCSIRead::DoPlayAudio", cdbSize == 12 ? "PLAY AUDIO (12)" : "PLAY AUDIO (10)");

    // Where to start reading (LBA)
    gadget->m_nblock_address = (u32)(gadget->m_CBW.CBWCB[2] << 24) | (u32)(gadget->m_CBW.CBWCB[3] << 16) | (u32)(gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

    if (cdbSize == 12)
    {
        // Number of blocks to read (LBA)
        gadget->m_nnumber_blocks = (u32)(gadget->m_CBW.CBWCB[6] << 24) | (u32)(gadget->m_CBW.CBWCB[7] << 16) | (u32)(gadget->m_CBW.CBWCB[8] << 8) | gadget->m_CBW.CBWCB[9];
    }
    else
    {
        // Number of blocks to read (LBA)
        gadget->m_nnumber_blocks = (u32)((gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8]);
    }

    CDROM_DEBUG_LOG("SCSIRead::DoPlayAudio", "PLAY AUDIO (%d) Playing from %lu for %lu blocks", cdbSize, gadget->m_nblock_address, gadget->m_nnumber_blocks);

    // Play the audio, but only if length > 0
    if (gadget->m_nnumber_blocks > 0)
    {
        CUETrackInfo trackInfo = CDUtils::GetTrackInfoForLBA(gadget, gadget->m_nblock_address);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer)
            {
                CDROM_DEBUG_LOG("SCSIRead::DoPlayAudio", "PLAY AUDIO (%d) Play command sent", cdbSize);
                if (gadget->m_nblock_address == 0xffffffff)
                    cdplayer->Resume();
                else
                    cdplayer->Play(gadget->m_nblock_address, gadget->m_nnumber_blocks);
            }
        }
        else
        {
            gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
            gadget->setSenseData(0x05, 0x64, 0x00); // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
        }
    }

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
    gadget->SendCSW();
}

void SCSIRead::PlayAudioMSF(CUSBCDGadget* gadget)
{
    // Start MSF
    u8 SM = gadget->m_CBW.CBWCB[3];
    u8 SS = gadget->m_CBW.CBWCB[4];
    u8 SF = gadget->m_CBW.CBWCB[5];

    // End MSF
    u8 EM = gadget->m_CBW.CBWCB[6];
    u8 ES = gadget->m_CBW.CBWCB[7];
    u8 EF = gadget->m_CBW.CBWCB[8];

    // Convert MSF to LBA
    u32 start_lba = CDUtils::msf_to_lba(SM, SS, SF);
    u32 end_lba = CDUtils::msf_to_lba(EM, ES, EF);
    int num_blocks = end_lba - start_lba;
    CDROM_DEBUG_LOG("SCSIRead::PlayAudioMSF", "PLAY AUDIO MSF. Start MSF %d:%d:%d, End MSF: %d:%d:%d, start LBA %u, end LBA %u", SM, SS, SF, EM, ES, EF, start_lba, end_lba);

    CUETrackInfo trackInfo = CDUtils::GetTrackInfoForLBA(gadget, start_lba);
    if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
    {
        // Play the audio
        CDROM_DEBUG_LOG("SCSIRead::PlayAudioMSF", "CD Player found, sending command");
        CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {
            if (start_lba == 0xFFFFFFFF)
            {
                CDROM_DEBUG_LOG("SCSIRead::PlayAudioMSF", "CD Player found, Resume");
                cdplayer->Resume();
            }
            else if (start_lba == end_lba)
            {
                CDROM_DEBUG_LOG("SCSIRead::PlayAudioMSF", "CD Player found, Pause");
                cdplayer->Pause();
            }
            else
            {
                CDROM_DEBUG_LOG("SCSIRead::PlayAudioMSF", "CD Player found, Play");
                cdplayer->Play(start_lba, num_blocks);
            }
        }
    }
    else
    {
        MLOGNOTE("SCSIRead::PlayAudioMSF", "PLAY AUDIO MSF: Not an audio track");
        gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // CD_CSW_STATUS_FAIL
        gadget->setSenseData(0x05, 0x64, 0x00); // ILLEGAL MODE FOR THIS TRACK OR INCOMPATIBLE MEDIUM
    }

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
    gadget->SendCSW();
}

void SCSIRead::Seek(CUSBCDGadget* gadget)
{
    // Where to start reading (LBA)
    gadget->m_nblock_address = (u32)(gadget->m_CBW.CBWCB[2] << 24) | (u32)(gadget->m_CBW.CBWCB[3] << 16) | (u32)(gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];

    CDROM_DEBUG_LOG("SCSIRead::Seek", "SEEK to LBA %lu", gadget->m_nblock_address);

    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Seek(gadget->m_nblock_address);
    }

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
    gadget->SendCSW();
}

void SCSIRead::PauseResume(CUSBCDGadget* gadget)
{
    MLOGNOTE("SCSIRead::PauseResume", "PAUSE/RESUME");
    int resume = gadget->m_CBW.CBWCB[8] & 0x01;

    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        if (resume)
            cdplayer->Resume();
        else
            cdplayer->Pause();
    }

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
    gadget->SendCSW();
}

void SCSIRead::StopScan(CUSBCDGadget* gadget)
{
    MLOGNOTE("SCSIRead::StopScan", "STOP / SCAN");

    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Pause();
    }

    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
    gadget->SendCSW();
}

void SCSIRead::ReadCD(CUSBCDGadget* gadget)
{
    if (!gadget->m_CDReady)
    {
        gadget->setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
        return;
    }

    int expectedSectorType = (gadget->m_CBW.CBWCB[1] >> 2) & 0x07;
    gadget->m_nblock_address = (gadget->m_CBW.CBWCB[2] << 24) | (gadget->m_CBW.CBWCB[3] << 16) |
                       (gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];
    gadget->m_nnumber_blocks = (gadget->m_CBW.CBWCB[6] << 16) | (gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8];
    gadget->mcs = (gadget->m_CBW.CBWCB[9] >> 3) & 0x1F;

    // Subchannel selection from byte 10
    u8 subChannelSelection = gadget->m_CBW.CBWCB[10] & 0x07;

    CDROM_DEBUG_LOG("SCSIRead::ReadCD",
                    "READ CD: USB=%s, LBA=%u, blocks=%u, type=0x%02x, MCS=0x%02x, subchan=0x%02x",
                    gadget->m_IsFullSpeed ? "FS" : "HS",
                    gadget->m_nblock_address, gadget->m_nnumber_blocks,
                    expectedSectorType, gadget->mcs, subChannelSelection);

    // Check subchannel request compatibility
    if (subChannelSelection != 0 && !gadget->m_pDevice->HasSubchannelData())
    {
        CDROM_DEBUG_LOG("SCSIRead::ReadCD",
                        "READ CD: Subchannel requested but image has no subchannel data");
        gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        gadget->sendCheckCondition();
        return;
    }

    // Get track info for validation
    CUETrackInfo trackInfo = CDUtils::GetTrackInfoForLBA(gadget, gadget->m_nblock_address);

    // Verify sector type if specified
    if (expectedSectorType != 0)
    {
        bool sector_type_ok = false;

        if (expectedSectorType == 1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            sector_type_ok = true; // CD-DA
        }
        else if (expectedSectorType == 2 &&
                 (trackInfo.track_mode == CUETrack_MODE1_2048 ||
                  trackInfo.track_mode == CUETrack_MODE1_2352))
        {
            sector_type_ok = true; // Mode 1
        }
        else if (expectedSectorType == 3 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true; // Mode 2 formless
        }
        else if (expectedSectorType == 4 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true; // Mode 2 form 1
        }
        else if (expectedSectorType == 5 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true; // Mode 2 form 2
        }

        if (!sector_type_ok)
        {
            CDROM_DEBUG_LOG("SCSIRead::ReadCD",
                            "READ CD: Sector type mismatch. Expected=%d, Track mode=%d",
                            expectedSectorType, trackInfo.track_mode);
            gadget->setSenseData(0x05, 0x64, 0x00); // ILLEGAL MODE FOR THIS TRACK
            gadget->sendCheckCondition();
            return;
        }
    }

    // Ensure read doesn't exceed image size
    u64 readEnd = (u64)gadget->m_nblock_address * trackInfo.sector_length +
                  (u64)gadget->m_nnumber_blocks * trackInfo.sector_length;
    if (readEnd > gadget->m_pDevice->GetSize())
    {
        MLOGNOTE("SCSIRead::ReadCD",
                 "READ CD: Read exceeds image size");
        gadget->setSenseData(0x05, 0x21, 0x00); // LOGICAL BLOCK ADDRESS OUT OF RANGE
        gadget->sendCheckCondition();
        return;
    }

    // Determine sector parameters based on expected type or track mode
    switch (expectedSectorType)
    {
    case 0x01: // CD-DA
        gadget->block_size = 2352;
        gadget->transfer_block_size = 2352;
        gadget->skip_bytes = 0;
        break;

    case 0x02: // Mode 1
        gadget->skip_bytes = CDUtils::GetSkipbytesForTrack(gadget, trackInfo);
        gadget->block_size = CDUtils::GetBlocksizeForTrack(gadget, trackInfo);
        gadget->transfer_block_size = 2048;
        break;

    case 0x03: // Mode 2 formless
        gadget->skip_bytes = 16;
        gadget->block_size = 2352;
        gadget->transfer_block_size = 2336;
        break;

    case 0x04: // Mode 2 form 1
        gadget->skip_bytes = CDUtils::GetSkipbytesForTrack(gadget, trackInfo);
        gadget->block_size = CDUtils::GetBlocksizeForTrack(gadget, trackInfo);
        gadget->transfer_block_size = 2048;
        break;

    case 0x05: // Mode 2 form 2
        gadget->block_size = 2352;
        gadget->skip_bytes = 24;
        gadget->transfer_block_size = 2328;
        break;

    case 0x00: // Type not specified - derive from MCS and track mode
    default:
        if (trackInfo.track_mode == CUETrack_AUDIO)
        {
            gadget->block_size = 2352;
            gadget->transfer_block_size = 2352;
            gadget->skip_bytes = 0;
        }
        else
        {
            gadget->block_size = CDUtils::GetBlocksizeForTrack(gadget, trackInfo);
            gadget->transfer_block_size = CDUtils::GetSectorLengthFromMCS(gadget->mcs);
            gadget->skip_bytes = CDUtils::GetSkipBytesFromMCS(gadget->mcs);
        }
        break;
    }

    // Add subchannel data size if requested
    if (subChannelSelection != 0)
    {
        // Subchannel selections:
        // 0x00 = No subchannel
        // 0x01 = Raw P-W (96 bytes)
        // 0x02 = Q subchannel (16 bytes) - formatted
        // 0x04 = P-W subchannel (96 bytes) - deinterleaved and error corrected

        CDROM_DEBUG_LOG("SCSIRead::ReadCD",
                        "READ CD: Adding subchannel data (type 0x%02x)", subChannelSelection);

        // Most requests are for raw P-W (96 bytes)
        if (subChannelSelection == 0x01)
        {
            gadget->transfer_block_size += 96; // Add raw P-W subchannel
        }
        else if (subChannelSelection == 0x02)
        {
            gadget->transfer_block_size += 16; // Add formatted Q subchannel
        }
        else
        {
            CDROM_DEBUG_LOG("SCSIRead::ReadCD",
                            "READ CD: Unsupported subchannel type 0x%02x", subChannelSelection);
            gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
            gadget->sendCheckCondition();
            return;
        }
    }

    gadget->m_nbyteCount = gadget->m_CBW.dCBWDataTransferLength;
    if (gadget->m_nnumber_blocks == 0)
    {
        gadget->m_nnumber_blocks = 1 + (gadget->m_nbyteCount) / gadget->transfer_block_size;
    }

    // Determine transfer mode once based on transfer parameters
    gadget->m_NeedsSubchannel = (subChannelSelection != 0 && gadget->m_pDevice->HasSubchannelData());
    
    if (gadget->transfer_block_size == gadget->block_size && gadget->skip_bytes == 0)
    {
        gadget->m_TransferMode = gadget->m_NeedsSubchannel ? 
            CUSBCDGadget::TransferMode::SIMPLE_COPY_SUBCHAN : 
            CUSBCDGadget::TransferMode::SIMPLE_COPY;
    }
    else if (gadget->transfer_block_size > gadget->block_size)
    {
        gadget->m_TransferMode = gadget->m_NeedsSubchannel ?
            CUSBCDGadget::TransferMode::SECTOR_REBUILD_SUBCHAN :
            CUSBCDGadget::TransferMode::SECTOR_REBUILD;
    }
    else
    {
        gadget->m_TransferMode = gadget->m_NeedsSubchannel ?
            CUSBCDGadget::TransferMode::SKIP_COPY_SUBCHAN :
            CUSBCDGadget::TransferMode::SKIP_COPY;
    }

    CDROM_DEBUG_LOG("SCSIRead::ReadCD", 
                    "Transfer mode: %d, subchannel: %d, block_size: %d, transfer_size: %d, skip: %d",
                    (int)gadget->m_TransferMode, gadget->m_NeedsSubchannel,
                    gadget->block_size, gadget->transfer_block_size, gadget->skip_bytes);

    gadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}