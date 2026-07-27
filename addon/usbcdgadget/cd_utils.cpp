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
// READ CD (MMC opcode 0xBE) names the parts of a sector the host wants in the
// top five bits of CDB byte 9, which SCSIRead::ReadCD extracts as
// (byte9 >> 3) & 0x1F. Those five bits are the sector's fields in the order
// they physically appear on the disc:
//
//   0x10  SYNC        0x08  SUBHEADER   0x04  HEADER
//   0x02  USER DATA   0x01  EDC/ECC
//
// Note that the header and the subheader bits are not in disc order. CDB byte 9
// bits 6..5 are a single two-bit Header Codes *value*, not two flags: 00 none,
// 01 header only, 10 subheader only, 11 both. After the >> 3 those land on 0x08
// and 0x04, so the low bit of the field - 0x04 - is the 4-byte header and the
// high bit - 0x08 - is the 8-byte subheader, even though the header physically
// comes first. Reading them in bit order instead of field order makes Windows
// XP's second request look like it skips the header when it does not.
//
// Their sizes are not fixed: they depend on what kind of sector it is, which
// is what TCDSectorShape carries. A field this sector kind does not have is
// zero, and a zero-size field is invisible - it can be neither served nor
// skipped, and it cannot split the fields on either side of it.
//
// Two defects lived here. The bottom three selection bits were read one
// position high, so the subheader bit was taken for user data, the user data
// bit for EDC/ECC, and the EDC/ECC bit was never examined at all: a host
// asking for user data only (byte 9 = 0x10, selection 0x02) was handed 288
// bytes of error-correction data where it wanted 2048 bytes of file content.
// Raw reads escaped it because selection 0x1F sets every bit, so the total
// came to 2352 whichever bit meant what - which is why the one mode Windows 98
// uses for Mode 2 files always worked while every other mode returned nonsense.
// And the sizes were hardcoded to Mode 1 Form 1, so a Mode 2 Form 2 sector,
// whose user data is 2324 bytes and whose EDC is 4, could never be described.

CDUtils::TCDSectorShape CDUtils::GetSectorShape(int expectedSectorType, CUETrackMode trackMode)
{
    int type = expectedSectorType;

    if (type == 0)
    {
        // Type not specified: take it from the track. A Mode 2 track is
        // assumed to be Form 1, because the form is a property of the
        // individual sector and one command serves a single layout. A host
        // that actually wants Form 2 says so in CDB byte 1, which is the path
        // Windows XP uses.
        if (trackMode == CUETrack_AUDIO || trackMode == CUETrack_CDG)
            type = 1;
        else if (trackMode == CUETrack_MODE1_2048 || trackMode == CUETrack_MODE1_2352)
            type = 2;
        else
            type = 4;
    }

    TCDSectorShape shape;
    switch (type)
    {
    case 1: // CD-DA: no structure at all, the whole sector is sample data
        shape.nSync = 0;  shape.nHeader = 0; shape.nSubheader = 0;
        shape.nUserData = 2352; shape.nEdcEcc = 0;
        break;

    case 3: // Mode 2 formless: no subheader, and the tail is user data
        shape.nSync = 12; shape.nHeader = 4; shape.nSubheader = 0;
        shape.nUserData = 2336; shape.nEdcEcc = 0;
        break;

    case 4: // Mode 2 Form 1: the 8 bytes Mode 1 reserves are the subheader,
            // which is why the EDC/ECC tail is 280 rather than 288
        shape.nSync = 12; shape.nHeader = 4; shape.nSubheader = 8;
        shape.nUserData = 2048; shape.nEdcEcc = 280;
        break;

    case 5: // Mode 2 Form 2: real-time data, 2324 bytes and only an EDC
        shape.nSync = 12; shape.nHeader = 4; shape.nSubheader = 8;
        shape.nUserData = 2324; shape.nEdcEcc = 4;
        break;

    case 2: // Mode 1
    default:
        shape.nSync = 12; shape.nHeader = 4; shape.nSubheader = 0;
        shape.nUserData = 2048; shape.nEdcEcc = 288;
        break;
    }

    return shape;
}

// The transfer is served as one contiguous slice of the sector, so the fields
// the host asks for have to be adjacent. Asking for the sync and the user data
// but not the header between them cannot be expressed as an offset and a
// length, and quietly including the header would be worse than refusing.
bool CDUtils::McsFieldsAreContiguous(uint8_t mainChannelSelection, const TCDSectorShape& shape)
{
    const int sizes[5] = {shape.nSync, shape.nHeader, shape.nSubheader,
                          shape.nUserData, shape.nEdcEcc};
    // Disc order: sync, header, subheader, user data, EDC/ECC. The header is
    // 0x04 and the subheader 0x08 - see the Header Codes note above.
    const uint8_t bits[5] = {0x10, 0x04, 0x08, 0x02, 0x01};

    bool bStarted = false;
    bool bEnded = false;

    for (int i = 0; i < 5; i++)
    {
        if (sizes[i] == 0)
            continue; // absent from this sector kind, so it breaks nothing

        if (mainChannelSelection & bits[i])
        {
            if (bEnded)
                return false; // a second run after a gap
            bStarted = true;
        }
        else if (bStarted)
        {
            bEnded = true;
        }
    }

    return true;
}

int CDUtils::GetSectorLengthFromMCS(uint8_t mainChannelSelection, const TCDSectorShape& shape)
{
    int total = 0;
    if (mainChannelSelection & 0x10)
        total += shape.nSync;
    if (mainChannelSelection & 0x04)
        total += shape.nHeader; // Header Codes low bit
    if (mainChannelSelection & 0x08)
        total += shape.nSubheader; // Header Codes high bit
    if (mainChannelSelection & 0x02)
        total += shape.nUserData;
    if (mainChannelSelection & 0x01)
        total += shape.nEdcEcc;

    return total;
}

int CDUtils::GetSkipBytesFromMCS(uint8_t mainChannelSelection, const TCDSectorShape& shape)
{
    // Skip the leading fields the host did not ask for and stop at the first
    // one it did. Skipping unconditionally walks past requested fields and
    // starts the slice in the wrong place, which is what handed Windows XP a
    // Mode 2 Form 2 sector beginning 24 bytes into the MPEG payload instead of
    // at the sync pattern it asked for.
    const int sizes[5] = {shape.nSync, shape.nHeader, shape.nSubheader,
                          shape.nUserData, shape.nEdcEcc};
    // Disc order: sync, header, subheader, user data, EDC/ECC. The header is
    // 0x04 and the subheader 0x08 - see the Header Codes note above.
    const uint8_t bits[5] = {0x10, 0x04, 0x08, 0x02, 0x01};

    int offset = 0;
    for (int i = 0; i < 5; i++)
    {
        if (sizes[i] == 0)
            continue;
        if (mainChannelSelection & bits[i])
            break;
        offset += sizes[i];
    }

    return offset;
}
