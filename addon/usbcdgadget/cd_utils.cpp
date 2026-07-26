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

uint8_t CDUtils::ToBCD(int value)
{
    if (value < 0)
        value = 0;
    if (value > 99)
        value = 99;
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

void CDUtils::LBA2MSFBCD(int32_t LBA, uint8_t *MSF, bool relative)
{
    LBA2MSF(LBA, MSF, relative);
    MSF[0] = ToBCD(MSF[0]);
    MSF[1] = ToBCD(MSF[1]);
    MSF[2] = ToBCD(MSF[2]);
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

// Work out which track begins the last session.
//
// Two sources, in order of trust. A cue sheet that carries "REM SESSION"
// markers has told us outright, so believe it. Merging tools routinely drop
// those markers, though, leaving a CD Extra indistinguishable from a plain
// mixed-mode disc except by its layout: audio tracks first, then a data track.
// A data track that follows audio can only be the start of a later session,
// because a session's tracks are contiguous and a disc never returns to audio
// after data within one session.
//
// Returns the first track number of the last session, which is the first track
// on the disc for an ordinary single-session image (data-first mixed mode
// included, since no data track there follows audio).
int CDUtils::GetLastSessionStartTrack(CUSBCDGadget* gadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    int firstTrack = -1;
    int markedStart = -1;
    int maxSession = 1;
    int inferredStart = -1;
    bool prevWasAudio = false;
    bool anyAudio = false;

    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (firstTrack < 0)
            firstTrack = trackInfo->track_number;

        if (trackInfo->session > maxSession)
        {
            maxSession = trackInfo->session;
            markedStart = trackInfo->track_number;
        }

        bool isAudio = (trackInfo->track_mode == CUETrack_AUDIO);
        if (!isAudio && prevWasAudio && anyAudio)
            inferredStart = trackInfo->track_number;

        prevWasAudio = isAudio;
        if (isAudio)
            anyAudio = true;
    }

    if (firstTrack < 0)
        return 1; // No tracks at all; caller handles the empty case

    if (markedStart > 0)
        return markedStart;

    if (inferredStart > 0)
        return inferredStart;

    return firstTrack;
}

int CDUtils::GetSessionCount(CUSBCDGadget* gadget)
{
    const CUETrackInfo *trackInfo = nullptr;
    int firstTrack = -1;

    gadget->cueParser.restart();
    if ((trackInfo = gadget->cueParser.next_track()) != nullptr)
        firstTrack = trackInfo->track_number;

    if (firstTrack < 0)
        return 1;

    // We only ever describe one or two sessions. Real CD Extra discs are two,
    // and the multi-session images this firmware serves have never carried a
    // third, so a session count is really "does a later session exist".
    return (GetLastSessionStartTrack(gadget) != firstTrack) ? 2 : 1;
}

// Lead-out position of a given session.
//
// The last session's lead-out is the disc lead-out. Session 1 of a two-session
// disc is the interesting case: its lead-out sits at the end of the last audio
// track, and between it and the next session's first track lie session 1's
// lead-out area and session 2's lead-in -- around 11250 frames carrying no
// track data. Reporting session 1's lead-out at the next session's first track
// describes a disc where the two sessions touch, which cannot physically
// happen, so hosts are entitled to reject the whole session structure.
//
// The gap cannot be measured from a single-file cue: INDEX times are
// file-relative, the gap is stored as ordinary sectors, and the byte distance
// between tracks therefore always equals the LBA distance. Only a
// "REM LEAD-OUT" marker states it. Without one, fall back on the standard gap
// so the layout is at least structurally valid.
u32 CDUtils::GetSessionLeadoutLBA(CUSBCDGadget* gadget, int session)
{
    // Orange Book: 90 seconds of lead-out (6750 frames) then 60 seconds of
    // lead-in (4500). Real CD Extra discs sit at or just above this.
    static const u32 kSessionGapFrames = 11250;

    if (session != 1 || GetSessionCount(gadget) < 2)
        return GetLeadoutLBA(gadget);

    int nextSessionStart = GetLastSessionStartTrack(gadget);

    const CUETrackInfo *trackInfo = nullptr;
    CUETrackInfo lastOfSession = {};
    CUETrackInfo firstOfNext = {};
    bool haveLast = false;
    bool haveNext = false;

    gadget->cueParser.restart();
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_number < nextSessionStart)
        {
            lastOfSession = *trackInfo;
            haveLast = true;
        }
        else if (trackInfo->track_number == nextSessionStart)
        {
            firstOfNext = *trackInfo;
            haveNext = true;
        }
    }

    if (!haveLast || !haveNext)
        return GetLeadoutLBA(gadget);

    // The cue sheet said so outright. This is the only exact source.
    if (firstOfNext.prev_session_leadout > lastOfSession.data_start &&
        firstOfNext.prev_session_leadout < firstOfNext.track_start)
    {
        return firstOfNext.prev_session_leadout;
    }

    // No marker: assume the standard gap. Guard against a merged image whose
    // sessions sit closer together than that, where subtracting the full gap
    // would put the lead-out inside the last audio track -- there, place it at
    // the start of the next session's pregap, the latest defensible position.
    if (firstOfNext.track_start > lastOfSession.data_start + kSessionGapFrames)
        return firstOfNext.track_start - kSessionGapFrames;

    return firstOfNext.track_start;
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
    if (trackInfo == nullptr)
    {
        // No parseable tracks: an empty .cue, one whose TRACK lines never
        // made it, or a file that is not a cue sheet at all. Fall back to a
        // zeroed track, which is what GetTrackInfoForLBA() hands the rest of
        // the code for the same image, so the whole mount stays consistent
        // instead of faulting here.
        CUETrackInfo empty = {};
        return GetBlocksizeForTrack(gadget, empty);
    }
    return GetBlocksizeForTrack(gadget, *trackInfo);
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
    if (trackInfo == nullptr)
    {
        // See GetBlocksize(): same unparseable-cue case, same fallback.
        CUETrackInfo empty = {};
        return GetSkipbytesForTrack(gadget, empty);
    }
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

int CDUtils::GetMediumType(CUSBCDGadget* gadget)
{
    // Report the actual medium type (SFF-8020i codes) for every target OS.
    // Win9x MCICDA reads this byte from the MODE SENSE header to decide
    // whether a disc can play audio, and only accepts the classic codes
    // (0x02 audio, 0x03 data+audio): the previous hardcoded 0x13
    // ("CD-R data & audio") made it treat every disc as data-only
    // ("data or no disc loaded", PLAY AUDIO never issued). Modern hosts
    // ignore this byte in favor of GET CONFIGURATION.
    bool hasAudio = false;
    bool hasData = false;
    
    gadget->cueParser.restart();
    const CUETrackInfo *trackInfo = nullptr;
    
    while ((trackInfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (trackInfo->track_mode == CUETrack_AUDIO)
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
