#include "cdrom_util.h"
#include "usbcdgadget.h"
#include <circle/logger.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

u8 BCD(u8 val)
{
    return ((val / 10) << 4) | (val % 10);
}

u32 msf_to_lba(u8 m, u8 s, u8 f)
{
    return (m * 60 * 75) + (s * 75) + f;
}

void LBA2MSF(u32 lba, u8 *msf, boolean is_bcd)
{
    lba += 150;
    msf[0] = lba / (60 * 75);
    msf[1] = (lba / 75) % 60;
    msf[2] = lba % 75;

    if (is_bcd)
    {
        msf[0] = BCD(msf[0]);
        msf[1] = BCD(msf[1]);
        msf[2] = BCD(msf[2]);
    }
}

void LBA2MSFBCD(u32 lba, u8 *msf, boolean is_bcd)
{
    lba += 150;
    msf[0] = BCD(lba / (60 * 75));
    msf[1] = BCD((lba / 75) % 60);
    msf[2] = BCD(lba % 75);
}

u32 GetAddress(u32 address, bool msf, bool relative)
{
    if (msf)
    {
        u32 bcd_address = 0;
        u8 *dest = (u8 *)&bcd_address;
        if (relative)
            LBA2MSFBCD(address, dest, true);
        else
            LBA2MSF(address, dest, true);
        return bcd_address;
    }
    else
    {
        return __builtin_bswap32(address);
    }
}

int GetBlocksize(CUSBCDGadget* pGadget)
{
    pGadget->cueParser.restart();
    const CUETrackInfo *trackInfo = pGadget->cueParser.next_track();
    return GetBlocksizeForTrack(*trackInfo);
}

int GetBlocksizeForTrack(CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
        return 2048;
    case CUETrack_MODE1_2352:
        return 2352;
    case CUETrack_MODE2_2352:
        return 2352;
    case CUETrack_AUDIO:
        return 2352;
    default:
        return 0;
    }
}

int GetSkipbytes(CUSBCDGadget* pGadget)
{
    pGadget->cueParser.restart();
    const CUETrackInfo *trackInfo = pGadget->cueParser.next_track();
    return GetSkipbytesForTrack(*trackInfo);
}

int GetSkipbytesForTrack(CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
        return 0;
    case CUETrack_MODE1_2352:
        return 16;
    case CUETrack_MODE2_2352:
        return 24;
    case CUETrack_AUDIO:
        return 0;
    default:
        return 0;
    }
}

int GetMediumType(CUSBCDGadget* pGadget)
{
    pGadget->cueParser.restart();
    const CUETrackInfo *trackInfo = nullptr;
    pGadget->cueParser.restart();
    while ((trackInfo = pGadget->cueParser.next_track()) != nullptr)
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

CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* pGadget, int track)
{
    const CUETrackInfo *trackInfo = nullptr;
    pGadget->cueParser.restart();
    while ((trackInfo = pGadget->cueParser.next_track()) != nullptr)
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

CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* pGadget, u32 lba)
{
    const CUETrackInfo *trackInfo;

    pGadget->cueParser.restart();

    // Shortcut for LBA zero
    if (lba == 0)
    {
        const CUETrackInfo *firstTrack = pGadget->cueParser.next_track(); // Return the first track
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
    while ((trackInfo = pGadget->cueParser.next_track()) != nullptr)
    {
        //  Shortcut for when our LBA is the start address of this track
        if (trackInfo->track_start == lba)
        {
            return *trackInfo;
        }

        if (lba < trackInfo->track_start)
        {
            return lastTrack;
        }

        lastTrack = *trackInfo;
    }

    return lastTrack;
}

u32 GetLeadoutLBA(CUSBCDGadget* pGadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    u32 file_offset = 0;
    u32 sector_length = 0;
    u32 track_start = 0;

    // Find the last track
    pGadget->cueParser.restart();
    while ((trackInfo = pGadget->cueParser.next_track()) != nullptr)
    {
        file_offset = trackInfo->file_offset;
        sector_length = trackInfo->sector_length;
        track_start = trackInfo->data_start; // I think this is right
    }

    u64 deviceSize = pGadget->m_pDevice->GetSize(); // Use u64 to support DVDs > 4GB

    if (deviceSize < file_offset)
    {
        return track_start;
    }

    if (sector_length == 0)
    {
        return track_start;
    }

    u64 remainingBytes = deviceSize - file_offset;
    u64 lastTrackBlocks = remainingBytes / sector_length;

    if (lastTrackBlocks > 0xFFFFFFFF)
    {
        lastTrackBlocks = 0xFFFFFFFF;
    }

    u32 ret = track_start + (u32)lastTrackBlocks;

    return ret;
}

int GetLastTrackNumber(CUSBCDGadget* pGadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    int lastTrack = 1;
    pGadget->cueParser.restart();
    while ((trackInfo = pGadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number > lastTrack)
            lastTrack = trackInfo->track_number;
    }
    return lastTrack;
}
