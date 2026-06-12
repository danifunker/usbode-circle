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

CUETrackInfo CDUtils::GetTrackInfoForLBA(CUSBCDGadget* gadget, u32 lba, u32 *pEndLBA)
{
    // An LBA between a track's track_start (INDEX 00) and the next track
    // belongs to that track; LBAs at or past the leadout map to the last
    // track, matching the previous parser-based behavior.
    for (int i = 0; i < gadget->m_nTrackCount; i++)
    {
        const CUSBCDGadget::CDTrackEntry &entry = gadget->m_TrackTable[i];

        if (lba < entry.info.track_start && !(lba == 0 && i == 0))
            break; // Below the first track's start: no match

        if (lba < entry.end_lba || i == gadget->m_nTrackCount - 1 || lba == 0)
        {
            if (pEndLBA)
                *pEndLBA = entry.end_lba;
            return entry.info;
        }
    }

    CUETrackInfo invalid = {};
    invalid.track_number = -1;
    if (pEndLBA)
        *pEndLBA = 0;
    return invalid;
}

CUETrackInfo CDUtils::GetTrackInfoForTrack(CUSBCDGadget* gadget, int track)
{
    for (int i = 0; i < gadget->m_nTrackCount; i++)
    {
        if (gadget->m_TrackTable[i].info.track_number == track)
        {
            return gadget->m_TrackTable[i].info;
        }
    }

    CUETrackInfo invalid = {};
    invalid.track_number = -1;
    return invalid;
}

int CDUtils::GetLastTrackNumber(CUSBCDGadget* gadget)
{
    if (gadget->m_nTrackCount == 0)
        return 1;
    return gadget->m_TrackTable[gadget->m_nTrackCount - 1].info.track_number;
}

u32 CDUtils::GetLeadoutLBA(CUSBCDGadget* gadget)
{
    return gadget->m_nLeadoutLBA;
}

int CDUtils::GetBlocksizeForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo)
{
    CDROM_DEBUG_LOG("CDUtils::GetBlocksizeForTrack", "Called with mode=%d, target=%s", trackInfo.track_mode, gadget->m_USBTargetOS);
    // FORCE RAW MODE for compatibility with .bin files that include headers when targeting macOS
    // if (gadget->m_USBTargetOS == USBTargetOS::Apple) == 0 && trackInfo.track_mode == CUETrack_MODE1_2048)
    // {
    //     CDROM_DEBUG_LOG("CDUtils::GetBlocksizeForTrack", "FORCE RAW MODE (2352) for Apple target OS");
    //     return 2352;
    // }

    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
    case CUETrack_MODE2_2048:
        return 2048;
    case CUETrack_MODE1_2352:
    case CUETrack_MODE2_2352:
    case CUETrack_CDI_2352:
    case CUETrack_AUDIO:
        return 2352;
    case CUETrack_MODE2_2336:
    case CUETrack_CDI_2336:
        return 2336;
    default:
        MLOGERR("CDUtils::GetBlocksizeForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

int CDUtils::GetSkipbytesForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo)
{
    switch (trackInfo.track_mode)
    {
    case CUETrack_MODE1_2048:
    case CUETrack_MODE2_2048:
    case CUETrack_AUDIO:
        return 0;
    case CUETrack_MODE1_2352:
        return 16; // sync (12) + header (4)
    case CUETrack_MODE2_2352:
    case CUETrack_CDI_2352:
        return 24; // sync (12) + header (4) + subheader (8)
    case CUETrack_MODE2_2336:
    case CUETrack_CDI_2336:
        return 8; // stored without sync/header; skip subheader only
    default:
        CDROM_DEBUG_LOG("CDUtils::GetSkipbytesForTrack", "Track mode %d not handled", trackInfo.track_mode);
        return 0;
    }
}

int CDUtils::GetMediumType(CUSBCDGadget* gadget)
{
    // Modern MMC: Medium Type should be 0x00, rely on GET CONFIGURATION
    if (gadget->m_USBTargetOS != USBTargetOS::Apple)
    {
        return 0x13;  // Modern drives return 0x13
    }
    
    // Legacy Mac OS 9 needs actual detection
    bool hasAudio = false;
    bool hasData = false;

    for (int i = 0; i < gadget->m_nTrackCount; i++)
    {
        if (gadget->m_TrackTable[i].info.track_mode == CUETrack_AUDIO)
            hasAudio = true;
        else
            hasData = true;
    }
    
    if (hasAudio && hasData)
        return 0x03;  // Mixed mode
    else if (hasAudio)
        return 0x02;  // Audio CD
    else
        return 0x01;  // Data CD
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
