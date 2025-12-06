//
// cd_utils.cpp
//
// CD-ROM Utility Functions and Calculations
//
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

// ============================================================================
// Address Conversion Utilities (BlueSCSI-inspired)
// ============================================================================

void CDUtils::LBA2MSF(int32_t LBA, uint8_t *MSF, bool relative)
{
    if (!relative)
    {
        LBA += 150; // Add 2-second pregap for absolute addressing
    }

    uint32_t ulba = LBA;
    if (LBA < 0)
    {
        ulba = LBA * -1;
    }

    MSF[2] = ulba % 75; // Frames
    uint32_t rem = ulba / 75;

    MSF[1] = rem % 60; // Seconds
    MSF[0] = rem / 60; // Minutes
}

void CDUtils::LBA2MSFBCD(int32_t LBA, uint8_t *MSF, bool relative)
{
    LBA2MSF(LBA, MSF, relative);
    MSF[0] = ((MSF[0] / 10) << 4) | (MSF[0] % 10);
    MSF[1] = ((MSF[1] / 10) << 4) | (MSF[1] % 10);
    MSF[2] = ((MSF[2] / 10) << 4) | (MSF[2] % 10);
}

int32_t CDUtils::MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative)
{
    int32_t lba = (m * 60 + s) * 75 + f;
    if (!relative)
        lba -= 150;
    return lba;
}

u32 CDUtils::GetAddress(u32 lba, int msf, boolean relative)
{
    if (msf)
    {
        uint8_t msfBytes[3];
        LBA2MSF(lba, msfBytes, relative);
        // Return as big-endian: frames|seconds|minutes|reserved
        return (msfBytes[2] << 24) | (msfBytes[1] << 16) | (msfBytes[0] << 8) | 0x00;
    }
    return htonl(lba);
}

u32 CDUtils::msf_to_lba(u8 minutes, u8 seconds, u8 frames)
{
    // Combine minutes, seconds, and frames into a single LBA-like value
    // The u8 inputs will be promoted to int/u32 for the arithmetic operations
    u32 lba = ((u32)minutes * 60 * 75) + ((u32)seconds * 75) + (u32)frames;

    // Adjust for the 150-frame (2-second) offset.
    lba = lba - 150;

    return lba;
}

u32 CDUtils::lba_to_msf(u32 lba, boolean relative)
{
    if (!relative)
        lba = lba + 150; // MSF values are offset by 2mins. Weird

    u8 minutes = lba / (75 * 60);
    u8 seconds = (lba / 75) % 60;
    u8 frames = lba % 75;
    u8 reserved = 0;

    return (frames << 24) | (seconds << 16) | (minutes << 8) | reserved;
}

// ============================================================================
// Track Info & Calculation
// ============================================================================

CUETrackInfo CDUtils::GetTrackInfoForLBA(CUSBCDGadget* gadget, u32 lba)
{
    const CUETrackInfo *trackInfo;
    MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Searching for LBA %u", lba);

    gadget->cueParser.restart();

    // Shortcut for LBA zero
    if (lba == 0)
    {
        MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Shortcut lba == 0 returning first track");
        const CUETrackInfo *firstTrack = gadget->cueParser.next_track(); // Return the first track
        if (firstTrack != nullptr)
        {
            return *firstTrack;
        }
        else
        {
            CUETrackInfo invalid = {};
            invalid.track_number = -1;
            return invalid;
        }
    }

    // Iterate to find our track
    CUETrackInfo lastTrack = {};
    lastTrack.track_number = -1;
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Iterating: Current Track %d track_start is %lu", trackInfo->track_number, trackInfo->track_start);

        //  Shortcut for when our LBA is the start address of this track
        if (trackInfo->track_start == lba)
        {
            MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Shortcut track_start == lba, returning track %d", trackInfo->track_number);
            return *trackInfo;
        }

        if (lba < trackInfo->track_start)
        {
            MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Found LBA %lu in track %d", lba, lastTrack.track_number);
            return lastTrack;
        }

        lastTrack = *trackInfo;
    }

    MLOGDEBUG("CDUtils::GetTrackInfoForLBA", "Returning last track");
    return lastTrack;
}

CUETrackInfo CDUtils::GetTrackInfoForTrack(CUSBCDGadget* gadget, int track)
{
    const CUETrackInfo *trackInfo = nullptr;
    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number == track)
        {
            return *trackInfo; // Safe copy — all fields are POD
        }
    }

    CUETrackInfo invalid = {};
    invalid.track_number = -1;
    return invalid;
}

int CDUtils::GetLastTrackNumber(CUSBCDGadget* gadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    int lastTrack = 1;
    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number > lastTrack)
            lastTrack = trackInfo->track_number;
    }
    return lastTrack;
}

u32 CDUtils::GetLeadoutLBA(CUSBCDGadget* gadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    u32 file_offset = 0;
    u32 sector_length = 0;
    u32 track_start = 0;

    // Find the last track
    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        file_offset = trackInfo->file_offset;
        sector_length = trackInfo->sector_length;
        track_start = trackInfo->data_start; // I think this is right
    }

    u64 deviceSize = gadget->m_pDevice->GetSize(); // Use u64 to support DVDs > 4GB

    // Some corrupted cd images might have a cue that references track that are
    // outside the bin.
    if (deviceSize < file_offset)
    {
        CDROM_DEBUG_LOG("CDUtils::GetLeadoutLBA",
                        "device size %llu < file_offset %lu, returning track_start %lu",
                        deviceSize, (unsigned long)file_offset, (unsigned long)track_start);
        return track_start;
    }

    // Guard against invalid sector length
    if (sector_length == 0)
    {
        MLOGERR("CDUtils::GetLeadoutLBA",
                "sector_length is 0, returning track_start %lu", (unsigned long)track_start);
        return track_start;
    }

    // We know the start position of the last track, and we know its sector length
    // and we know the file size, so we can work out the LBA of the end of the last track
    // We can't just divide the file size by sector size because sectors lengths might
    // not be consistent (e.g. multi-mode cd where track 1 is 2048
    u64 remainingBytes = deviceSize - file_offset;
    u64 lastTrackBlocks = remainingBytes / sector_length;

    // Ensure the result fits in u32 before casting
    if (lastTrackBlocks > 0xFFFFFFFF)
    {
        MLOGERR("CDUtils::GetLeadoutLBA",
                "lastTrackBlocks overflow: %llu, capping to max u32", lastTrackBlocks);
        lastTrackBlocks = 0xFFFFFFFF;
    }

    u32 ret = track_start + (u32)lastTrackBlocks; // Cast back to u32 for LBA (max ~2TB disc)

    CDROM_DEBUG_LOG("CDUtils::GetLeadoutLBA",
                    "device size is %llu, last track file offset is %lu, last track sector_length is %lu, "
                    "last track track_start is %lu, lastTrackBlocks = %llu, returning = %lu",
                    deviceSize, (unsigned long)file_offset, (unsigned long)sector_length,
                    (unsigned long)track_start, lastTrackBlocks, (unsigned long)ret);

    return ret;
}

int CDUtils::GetBlocksize(CUSBCDGadget* gadget)
{
    gadget->cueParser.restart();
    const CUETrackInfo *trackInfo = gadget->cueParser.next_track();
    return GetBlocksizeForTrack(gadget, *trackInfo);
}

int CDUtils::GetBlocksizeForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo)
{
    CDROM_DEBUG_LOG("CDUtils::GetBlocksizeForTrack", "Called with mode=%d, target=%s", trackInfo.track_mode, gadget->m_USBTargetOS);
    // FORCE RAW MODE for compatibility with .bin files that include headers when targeting macOS
    // if (strcmp(gadget->m_USBTargetOS, "apple") == 0 && trackInfo.track_mode == CUETrack_MODE1_2048)
    // {
    //     CDROM_DEBUG_LOG("CDUtils::GetBlocksizeForTrack", "FORCE RAW MODE (2352) for Apple target OS");
    //     return 2352;
    // }

    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
        MLOGNOTE("CDUtils::GetBlocksizeForTrack", "CUETrack_MODE1_2048");
        return 2048;
    case CUETrack_MODE1_2352:
        MLOGNOTE("CDUtils::GetBlocksizeForTrack", "CUETrack_MODE1_2352");
        return 2352;
    case CUETrack_MODE2_2352:
        MLOGNOTE("CDUtils::GetBlocksizeForTrack", "CUETrack_MODE2_2352");
        return 2352;
    case CUETrack_AUDIO:
        MLOGNOTE("CDUtils::GetBlocksizeForTrack", "CUETrack_AUDIO");
        return 2352;
    default:
        MLOGERR("CDUtils::GetBlocksizeForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

int CDUtils::GetSkipbytes(CUSBCDGadget* gadget)
{
    gadget->cueParser.restart();
    const CUETrackInfo *trackInfo = gadget->cueParser.next_track();
    return GetSkipbytesForTrack(gadget, *trackInfo);
}

int CDUtils::GetSkipbytesForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "CUETrack_MODE1_2048");
        return 0;
    case CUETrack_MODE1_2352:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "CUETrack_MODE1_2352");
        return 16;
    case CUETrack_MODE2_2352:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "CUETrack_MODE2_2352");
        return 24;
    case CUETrack_AUDIO:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "CUETrack_AUDIO");
        return 0;
    default:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

// Make an assumption about media type based on track 1 mode
int CDUtils::GetMediumType(CUSBCDGadget* gadget)
{
    gadget->cueParser.restart();
    const CUETrackInfo *trackInfo = nullptr;
    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number == 1 && trackInfo->track_mode == CUETrack_AUDIO)
            // Audio CD
            return 0x02;
        else if (trackInfo->track_number > 1)
            // Mixed mode
            return 0x03;
    }
    // Must be a data cd
    return 0x01;
}

int CDUtils::GetSectorLengthFromMCS(uint8_t mainChannelSelection)
{
    int total = 0;
    if (mainChannelSelection & 0x10)
        total += 12; // SYNC
    if (mainChannelSelection & 0x08)
        total += 4; // HEADER
    if (mainChannelSelection & 0x04)
        total += 2048; // USER DATA
    if (mainChannelSelection & 0x02)
        total += 288; // EDC + ECC

    return total;
}

int CDUtils::GetSkipBytesFromMCS(uint8_t mainChannelSelection)
{
    int offset = 0;

    // Skip SYNC if not requested
    if (!(mainChannelSelection & 0x10))
        offset += 12;

    // Skip HEADER if not requested
    if (!(mainChannelSelection & 0x08))
        offset += 4;

    // USER DATA is next; if also not requested, skip 2048
    if (!(mainChannelSelection & 0x04))
        offset += 2048;

    // EDC/ECC is always at the end, so no skipping here — it doesn't affect offset
    //
    return offset;
}
