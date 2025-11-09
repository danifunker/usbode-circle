#include "scsi_command_dispatcher.h"
#include "usbcdgadget.h"
#include "usbcdgadgetendpoint.h"
#include "cdrom_util.h"
#include <scsitbservice/scsitbservice.h>
#include <cdplayer/cdplayer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/util.h>
#include <string.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)
#define CDROM_DEBUG_LOG(pGadget, From, ...) do { if (pGadget->m_bDebugLogging) MLOGNOTE(From, __VA_ARGS__); } while (0)

// Helper function for TOC entry formatting
void CUSBCDGadget::FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0; // Reserved
    dest[1] = control_adr;
    dest[2] = track->track_number;
    dest[3] = 0; // Reserved

    if (use_MSF)
    {
        dest[4] = 0;
        LBA2MSF(track->data_start, &dest[5], false);
    }
    else
    {
        dest[4] = (track->data_start >> 24) & 0xFF;
        dest[5] = (track->data_start >> 16) & 0xFF;
        dest[6] = (track->data_start >> 8) & 0xFF;
        dest[7] = (track->data_start >> 0) & 0xFF;
    }
}

// Helper function for Raw TOC entry formatting
void CUSBCDGadget::FormatRawTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool useBCD)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0x01; // Session always 1
    dest[1] = control_adr;
    dest[2] = 0x00;                // TNO, always 0
    dest[3] = track->track_number; // POINT
    dest[4] = 0x00;                // ATIME (unused)
    dest[5] = 0x00;
    dest[6] = 0x00;
    dest[7] = 0; // HOUR

    if (useBCD)
    {
        LBA2MSFBCD(track->data_start, &dest[8], false);
    }
    else
    {
        LBA2MSF(track->data_start, &dest[8], false);
    }
}

// Complete READ TOC handler
void CUSBCDGadget::DoReadTOC(bool msf, uint8_t startingTrack, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG(this, "DoReadTOC", "Entry: msf=%d, startTrack=%d, allocLen=%d", msf, startingTrack, allocationLength);

    // NO SPECIAL CASE FOR 0xAA - let it flow through normally

    // Format track info
    uint8_t *trackdata = &m_InBuffer[4];
    int trackcount = 0;
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};

    CDROM_DEBUG_LOG(this, "DoReadTOC", "Building track list");
    cueParser.restart();
    const CUETrackInfo *trackinfo;
    while ((trackinfo = cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
            firsttrack = trackinfo->track_number;
        lasttrack = *trackinfo;

        // Include tracks >= startingTrack
        // Since 0xAA (170) is > any track number (1-99), this will SKIP all tracks when startingTrack=0xAA
        if (startingTrack == 0 || startingTrack <= trackinfo->track_number)
        {
            FormatTOCEntry(trackinfo, &trackdata[8 * trackcount], msf);

            CDROM_DEBUG_LOG(this, "DoReadTOC", "  Track %d: mode=%d, start=%u, msf=%d",
                            trackinfo->track_number, trackinfo->track_mode,
                            trackinfo->data_start, msf);

            trackcount++;
        }
    }

    // ALWAYS add leadout when startingTrack is 0 OR when we want tracks from startingTrack onwards
    // For startingTrack=0xAA: trackcount will be 0 here (no regular tracks added)
    // For startingTrack=0: trackcount will be 10 (all tracks added)
    CUETrackInfo leadout = {};
    leadout.track_number = 0xAA;
    leadout.track_mode = (lasttrack.track_number != 0) ? lasttrack.track_mode : CUETrack_MODE1_2048;
    leadout.data_start = GetLeadoutLBA(this);

    // Add leadout to the TOC
    FormatTOCEntry(&leadout, &trackdata[8 * trackcount], msf);

    CDROM_DEBUG_LOG(this, "DoReadTOC", "  Lead-out: LBA=%u", leadout.data_start);
    trackcount++;

    // Format header
    uint16_t toc_length = 2 + trackcount * 8;
    m_InBuffer[0] = (toc_length >> 8) & 0xFF;
    m_InBuffer[1] = toc_length & 0xFF;
    m_InBuffer[2] = firsttrack;
    m_InBuffer[3] = lasttrack.track_number;

    CDROM_DEBUG_LOG(this, "DoReadTOC", "Header: Length=%d, First=%d, Last=%d, Tracks=%d",
                    toc_length, firsttrack, lasttrack.track_number, trackcount);

    // Validation: when startingTrack is specified (not 0), we need at least the leadout
    if (startingTrack != 0 && startingTrack != 0xAA && trackcount < 2)
    {
        CDROM_DEBUG_LOG(this, "DoReadTOC", "INVALID: startTrack=%d but trackcount=%d", startingTrack, trackcount);
        setSenseData(0x05, 0x24, 0x00);
        sendCheckCondition();
        return;
    }

    uint32_t len = 2 + toc_length;
    if (len > allocationLength)
        len = allocationLength;

    // LOG RESPONSE BUFFER
    CDROM_DEBUG_LOG(this, "DoReadTOC", "Response (%d bytes, %d requested, full_size=%d):",
                    len, allocationLength, 2 + toc_length);
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG(this, "DoReadTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, m_InBuffer[i + 0], m_InBuffer[i + 1], m_InBuffer[i + 2], m_InBuffer[i + 3],
                            m_InBuffer[i + 4], m_InBuffer[i + 5], m_InBuffer[i + 6], m_InBuffer[i + 7],
                            m_InBuffer[i + 8], m_InBuffer[i + 9], m_InBuffer[i + 10], m_InBuffer[i + 11],
                            m_InBuffer[i + 12], m_InBuffer[i + 13], m_InBuffer[i + 14], m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG(this, "DoReadTOC", "%s", buf);
        }
    }

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadSessionInfo(bool msf, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG(this, "DoReadSessionInfo", "Entry: msf=%d, allocLen=%d", msf, allocationLength);

    uint8_t sessionTOC[12] = {
        0x00, 0x0A, 0x01, 0x01, 0x00, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00};

    cueParser.restart();
    const CUETrackInfo *trackinfo = cueParser.next_track();
    if (trackinfo)
    {
        CDROM_DEBUG_LOG(this, "DoReadSessionInfo", "First track: num=%d, start=%u",
                        trackinfo->track_number, trackinfo->data_start);

        if (msf)
        {
            sessionTOC[8] = 0;
            LBA2MSF(trackinfo->data_start, &sessionTOC[9], false);
            CDROM_DEBUG_LOG(this, "DoReadSessionInfo", "MSF: %02x:%02x:%02x",
                            sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
        else
        {
            sessionTOC[8] = (trackinfo->data_start >> 24) & 0xFF;
            sessionTOC[9] = (trackinfo->data_start >> 16) & 0xFF;
            sessionTOC[10] = (trackinfo->data_start >> 8) & 0xFF;
            sessionTOC[11] = (trackinfo->data_start >> 0) & 0xFF;
            CDROM_DEBUG_LOG(this, "DoReadSessionInfo", "LBA bytes: %02x %02x %02x %02x",
                            sessionTOC[8], sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
    }

    int len = sizeof(sessionTOC);
    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG(this, "DoReadSessionInfo", "Sending %d bytes", len);
    memcpy(m_InBuffer, sessionTOC, len);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD)
{
    CDROM_DEBUG_LOG(this, "DoReadFullTOC", "Entry: session=%d, allocLen=%d, BCD=%d",
                    session, allocationLength, useBCD);

    if (session > 1)
    {
        CDROM_DEBUG_LOG(this, "DoReadFullTOC", "INVALID SESSION %d", session);
        setSenseData(0x05, 0x24, 0x00);
        sendCheckCondition();
        return;
    }

    // Base full TOC structure with A0/A1/A2 descriptors
    uint8_t fullTOCBase[37] = {
        0x00, 0x2E, 0x01, 0x01,
        0x01, 0x14, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t len = sizeof(fullTOCBase);
    memcpy(m_InBuffer, fullTOCBase, len);

    // Find first and last tracks
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};
    const CUETrackInfo *trackinfo;

    cueParser.restart();
    while ((trackinfo = cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
        {
            firsttrack = trackinfo->track_number;
            if (trackinfo->track_mode == CUETrack_AUDIO)
            {
                m_InBuffer[5] = 0x10; // A0 control for audio
            }
            CDROM_DEBUG_LOG(this, "DoReadFullTOC", "First track: %d, mode=%d", firsttrack, trackinfo->track_mode);
        }
        lasttrack = *trackinfo;

        // Add track descriptor
        FormatRawTOCEntry(trackinfo, &m_InBuffer[len], useBCD);

        CDROM_DEBUG_LOG(this, "DoReadFullTOC", "  Track %d: mode=%d, start=%u",
                        trackinfo->track_number, trackinfo->track_mode, trackinfo->data_start);

        len += 11;
    }

    // Update A0, A1, A2 descriptors
    m_InBuffer[12] = firsttrack;
    m_InBuffer[23] = lasttrack.track_number;

    CDROM_DEBUG_LOG(this, "DoReadFullTOC", "A0: First=%d, A1: Last=%d", firsttrack, lasttrack.track_number);

    if (lasttrack.track_mode == CUETrack_AUDIO)
    {
        m_InBuffer[16] = 0x10; // A1 control
        m_InBuffer[27] = 0x10; // A2 control
    }

    // A2: Leadout position
    u32 leadoutLBA = GetLeadoutLBA(this);
    CDROM_DEBUG_LOG(this, "DoReadFullTOC", "A2: Lead-out LBA=%u", leadoutLBA);

    if (useBCD)
    {
        LBA2MSFBCD(leadoutLBA, &m_InBuffer[34], false);
        CDROM_DEBUG_LOG(this, "DoReadFullTOC", "A2 MSF (BCD): %02x:%02x:%02x",
                        m_InBuffer[34], m_InBuffer[35], m_InBuffer[36]);
    }
    else
    {
        LBA2MSF(leadoutLBA, &m_InBuffer[34], false);
        CDROM_DEBUG_LOG(this, "DoReadFullTOC", "A2 MSF: %02x:%02x:%02x",
                        m_InBuffer[34], m_InBuffer[35], m_InBuffer[36]);
    }

    // Update TOC length
    uint16_t toclen = len - 2;
    m_InBuffer[0] = (toclen >> 8) & 0xFF;
    m_InBuffer[1] = toclen & 0xFF;

    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG(this, "DoReadFullTOC", "Response: %d bytes (%d total, %d requested)",
                    len, toclen + 2, allocationLength);

    // LOG RESPONSE BUFFER
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG(this, "DoReadFullTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, m_InBuffer[i + 0], m_InBuffer[i + 1], m_InBuffer[i + 2], m_InBuffer[i + 3],
                            m_InBuffer[i + 4], m_InBuffer[i + 5], m_InBuffer[i + 6], m_InBuffer[i + 7],
                            m_InBuffer[i + 8], m_InBuffer[i + 9], m_InBuffer[i + 10], m_InBuffer[i + 11],
                            m_InBuffer[i + 12], m_InBuffer[i + 13], m_InBuffer[i + 14], m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG(this, "DoReadFullTOC", "%s", buf);
        }
    }

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadHeader(bool MSF, uint32_t lba, uint16_t allocationLength)
{
	CDROM_DEBUG_LOG(this, "DoReadHeader", "lba=%u, MSF=%d", lba, MSF);
    // Terminate audio playback if active (MMC Annex C requirement)
    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Pause();
    }

    uint8_t mode = 1; // Default to Mode 1

    cueParser.restart();
    CUETrackInfo trackinfo = GetTrackInfoForLBA(this, lba);

    if (trackinfo.track_number != -1 && trackinfo.track_mode == CUETrack_AUDIO)
    {
        mode = 0; // Audio track
    }

    m_InBuffer[0] = mode;
    m_InBuffer[1] = 0; // Reserved
    m_InBuffer[2] = 0; // Reserved
    m_InBuffer[3] = 0; // Reserved

    // Track start address
    if (MSF)
    {
        m_InBuffer[4] = 0;
        LBA2MSF(lba, &m_InBuffer[5], false);
    }
    else
    {
        m_InBuffer[4] = (lba >> 24) & 0xFF;
        m_InBuffer[5] = (lba >> 16) & 0xFF;
        m_InBuffer[6] = (lba >> 8) & 0xFF;
        m_InBuffer[7] = (lba >> 0) & 0xFF;
    }

    uint8_t len = 8;
    if (len > allocationLength)
        len = allocationLength;

    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, len);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void CUSBCDGadget::DoReadTrackInformation(u8 addressType, u32 address, u16 allocationLength)
{
    CDROM_DEBUG_LOG(this, "DoReadTrackInformation", "type=%d, addr=%u", addressType, address);
    TUSBCDTrackInformationBlock response;
    memset(&response, 0, sizeof(response));

    CUETrackInfo trackInfo = {0};
    trackInfo.track_number = -1;

    // Find the track based on address type
    if (addressType == 0x00)
    {
        // LBA address
        trackInfo = GetTrackInfoForLBA(this, address);
    }
    else if (addressType == 0x01)
    {
        // Logical track number
        trackInfo = GetTrackInfoForTrack(this, address);
    }
    else if (addressType == 0x02)
    {
        // Session number - we only support session 1
        if (address == 1)
        {
            cueParser.restart();
            const CUETrackInfo *first = cueParser.next_track();
            if (first)
                trackInfo = *first;
        }
    }

    if (trackInfo.track_number == -1)
    {
        setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        sendCheckCondition();
        return;
    }

    // Calculate track length
    u32 trackLength = 0;
    cueParser.restart();
    const CUETrackInfo *nextTrack = nullptr;
    const CUETrackInfo *currentTrack = nullptr;

    while ((currentTrack = cueParser.next_track()) != nullptr)
    {
        if (currentTrack->track_number == trackInfo.track_number)
        {
            nextTrack = cueParser.next_track();
            if (nextTrack)
            {
                trackLength = nextTrack->data_start - currentTrack->data_start;
            }
            else
            {
                // Last track - calculate from file size
                u32 leadoutLBA = GetLeadoutLBA(this);
                trackLength = leadoutLBA - currentTrack->data_start;
            }
            break;
        }
    }

    // Fill response
    response.dataLength = htons(0x002E); // 46 bytes
    response.logicalTrackNumberLSB = trackInfo.track_number;
    response.sessionNumberLSB = 0x01;

    // Track mode
    if (trackInfo.track_mode == CUETrack_AUDIO)
    {
        response.trackMode = 0x00; // Audio, 2 channels
        response.dataMode = 0x00;
    }
    else
    {
        response.trackMode = 0x04; // Data track, uninterrupted
        response.dataMode = 0x01;  // Mode 1
    }

    // Track start and size
    response.logicalTrackStartAddress = htonl(trackInfo.data_start);
    response.logicalTrackSize = htonl(trackLength);

    // Additional fields
    response.freeBlocks = htonl(0); // No free blocks (read-only disc)

    int length = sizeof(response);
    if (allocationLength < length)
        length = allocationLength;

    memcpy(m_InBuffer, &response, length);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, length);
    m_nState = TCDState::DataIn;
    m_nnumber_blocks = 0;
    m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::Dispatch(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    switch (pCBW->CBWCB[0])
    {
        case 0x00: HandleTestUnitReady(pGadget, pCBW); break;
        case 0x03: HandleRequestSense(pGadget, pCBW); break;
        case 0xa8: HandleRead12(pGadget, pCBW); break;
        case 0x12: HandleInquiry(pGadget, pCBW); break;
        case 0x1B: HandleStartStopUnit(pGadget, pCBW); break;
        case 0x1E: HandlePreventAllowMediumRemoval(pGadget, pCBW); break;
        case 0x25: HandleReadCapacity10(pGadget, pCBW); break;
        case 0x28: HandleRead10(pGadget, pCBW); break;
        case 0xBE: HandleReadCD(pGadget, pCBW); break;
        case 0xBB: HandleSetCDSpeed(pGadget, pCBW); break;
        case 0x2F: HandleVerify(pGadget, pCBW); break;
        case 0x43: HandleReadTOC(pGadget, pCBW); break;
        case 0x42: HandleReadSubChannel(pGadget, pCBW); break;
        case 0x52: HandleReadTrackInformation(pGadget, pCBW); break;
        case 0x4A: HandleGetEventStatusNotification(pGadget, pCBW); break;
        case 0xAD: HandleReadDiscStructure(pGadget, pCBW); break;
        case 0x51: HandleReadDiscInformation(pGadget, pCBW); break;
        case 0x44: HandleReadHeader(pGadget, pCBW); break;
        case 0x46: HandleGetConfiguration(pGadget, pCBW); break;
        case 0x4B: HandlePauseResume(pGadget, pCBW); break;
        case 0x2B: HandleSeek(pGadget, pCBW); break;
        case 0x47: HandlePlayAudioMSF(pGadget, pCBW); break;
        case 0x4E: HandleStopScan(pGadget, pCBW); break;
        case 0x45: HandlePlayAudio10(pGadget, pCBW); break;
        case 0xA5: HandlePlayAudio12(pGadget, pCBW); break;
        case 0x55: HandleModeSelect10(pGadget, pCBW); break;
        case 0x1a: HandleModeSense6(pGadget, pCBW); break;
        case 0x5a: HandleModeSense10(pGadget, pCBW); break;
        case 0xAC: HandleGetPerformance(pGadget, pCBW); break;
        case 0xa4: HandleA4(pGadget, pCBW); break;
        case 0xD9: HandleListDevices(pGadget, pCBW); break;
        case 0xD2: HandleNumberOfFiles(pGadget, pCBW); break;
        case 0xDA: HandleNumberOfFiles(pGadget, pCBW); break;
        case 0xD0: HandleListFiles(pGadget, pCBW); break;
        case 0xD7: HandleListFiles(pGadget, pCBW); break;
        case 0xD8: HandleSetNextCD(pGadget, pCBW); break;
        default: HandleUnknown(pGadget, pCBW); break;
    }
}

void ScsiCommandDispatcher::HandleTestUnitReady(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleTestUnitReady",
             "TEST UNIT READY: m_CDReady=%d, mediaState=%d, sense=%02x/%02x/%02x",
             pGadget->m_CDReady, (int)pGadget->m_mediaState,
             pGadget->m_SenseParams.bSenseKey, pGadget->m_SenseParams.bAddlSenseCode, pGadget->m_SenseParams.bAddlSenseCodeQual);

    if (!pGadget->m_CDReady)
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleTestUnitReady", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        pGadget->setSenseData(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT
        pGadget->m_mediaState = CUSBCDGadget::MediaState::NO_MEDIUM;
        pGadget->sendCheckCondition();
        return;
    }

    if (pGadget->m_mediaState == CUSBCDGadget::MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleTestUnitReady",
                 "TEST UNIT READY -> CHECK CONDITION (sense 06/28/00 - UNIT ATTENTION)");
        pGadget->setSenseData(0x06, 0x28, 0x00); // UNIT ATTENTION - MEDIA CHANGED
        pGadget->sendCheckCondition();
        return;
    }

    MLOGNOTE("ScsiCommandDispatcher::HandleTestUnitReady",
            "TEST UNIT READY -> GOOD STATUS");

    pGadget->sendGoodStatus();
}

void ScsiCommandDispatcher::HandleRequestSense(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u8 blocks = (u8)(pCBW->CBWCB[4]);

    MLOGNOTE("ScsiCommandDispatcher::HandleRequestSense",
            "REQUEST SENSE: mediaState=%d, sense=%02x/%02x/%02x -> reporting to host",
            (int)pGadget->m_mediaState,
            pGadget->m_SenseParams.bSenseKey, pGadget->m_SenseParams.bAddlSenseCode, pGadget->m_SenseParams.bAddlSenseCodeQual);

    u8 length = sizeof(TUSBCDRequestSenseReply);
    if (blocks < length)
        length = blocks;

    pGadget->m_ReqSenseReply.bSenseKey = pGadget->m_SenseParams.bSenseKey;
    pGadget->m_ReqSenseReply.bAddlSenseCode = pGadget->m_SenseParams.bAddlSenseCode;
    pGadget->m_ReqSenseReply.bAddlSenseCodeQual = pGadget->m_SenseParams.bAddlSenseCodeQual;

    memcpy(&pGadget->m_InBuffer, &pGadget->m_ReqSenseReply, length);

    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, length);

    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    pGadget->m_nState = CUSBCDGadget::TCDState::SendReqSenseReply;

    MLOGNOTE("ScsiCommandDispatcher::HandleRequestSense",
         "REQUEST SENSE: Clearing sense data after reporting");
    pGadget->clearSenseData();

    if (pGadget->m_mediaState == CUSBCDGadget::MediaState::MEDIUM_PRESENT_UNIT_ATTENTION)
    {
        pGadget->m_mediaState = CUSBCDGadget::MediaState::MEDIUM_PRESENT_READY;
        pGadget->bmCSWStatus = CD_CSW_STATUS_OK;
        MLOGNOTE("ScsiCommandDispatcher::HandleRequestSense",
                "REQUEST SENSE: State transition UNIT_ATTENTION -> READY");
    }
}

void ScsiCommandDispatcher::HandleRead12(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    if (pGadget->m_CDReady)
    {
        pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
        pGadget->m_nblock_address = (u32)(pCBW->CBWCB[2] << 24) | (u32)(pCBW->CBWCB[3] << 16) |
                           (u32)(pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
        pGadget->m_nnumber_blocks = (u32)(pCBW->CBWCB[6] << 24) | (u32)(pCBW->CBWCB[7] << 16) |
                               (u32)(pCBW->CBWCB[8] << 8) | pCBW->CBWCB[9];
        pGadget->transfer_block_size = 2048;
        pGadget->block_size = pGadget->data_block_size;
        pGadget->skip_bytes = pGadget->data_skip_bytes;
        pGadget->mcs = 0;
        pGadget->m_nbyteCount = pCBW->dCBWDataTransferLength;
        if (pGadget->m_nnumber_blocks == 0)
        {
            pGadget->m_nnumber_blocks = 1 + (pGadget->m_nbyteCount) / 2048;
        }
        pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
        pGadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
    }
    else
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleRead12", "READ(12) failed, %s", pGadget->m_CDReady ? "ready" : "not ready");
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
    }
}
void ScsiCommandDispatcher::HandleInquiry(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    int allocationLength = (pCBW->CBWCB[3] << 8) | pCBW->CBWCB[4];
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleInquiry", "Inquiry %0x, allocation length %d", pCBW->CBWCB[1], allocationLength);

    if ((pCBW->CBWCB[1] & 0x01) == 0)
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleInquiry", "Inquiry (Standard Enquiry)");

        int datalen = SIZE_INQR;
        if (allocationLength < datalen)
            datalen = allocationLength;

        memcpy(&pGadget->m_InBuffer, &pGadget->m_InqReply, datalen);
        pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, datalen);
        pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
        pGadget->m_nnumber_blocks = 0;
        pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    }
    else
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleInquiry", "Inquiry (VPD Inquiry)");
        u8 vpdPageCode = pCBW->CBWCB[2];
        switch (vpdPageCode)
        {
        case 0x00:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleInquiry", "Inquiry (Supported VPD Pages)");

            u8 SupportedVPDPageReply[] = {
                0x05, 0x00, 0x00, 0x03, 0x00, 0x80, 0x83
            };

            int datalen = sizeof(SupportedVPDPageReply);
            if (allocationLength < datalen)
                datalen = allocationLength;

            memcpy(&pGadget->m_InBuffer, &SupportedVPDPageReply, sizeof(SupportedVPDPageReply));
            pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       pGadget->m_InBuffer, datalen);
            pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            pGadget->m_nnumber_blocks = 0;
            pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }
        case 0x80:
        {
            MLOGNOTE("ScsiCommandDispatcher::HandleInquiry", "Inquiry (Unit Serial number Page)");

            u8 UnitSerialNumberReply[] = {
                0x05, 0x80, 0x00, 0x0B,
                'U', 'S', 'B', 'O', 'D', 'E', '0', '0', '0', '0', '1'
            };

            int datalen = sizeof(UnitSerialNumberReply);
            if (allocationLength < datalen)
                datalen = allocationLength;

            memcpy(&pGadget->m_InBuffer, &UnitSerialNumberReply, sizeof(UnitSerialNumberReply));
            pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       pGadget->m_InBuffer, datalen);
            pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            pGadget->m_nnumber_blocks = 0;
            pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }
        case 0x83:
        {
            u8 DeviceIdentificationReply[] = {
                0x05, 0x83, 0x00, 0x0B,
                0x01, 0x00, 0x08,
                'U', 'S', 'B', 'O', 'D', 'E', ' ', ' '
            };
            int datalen = sizeof(DeviceIdentificationReply);
            if (allocationLength < datalen)
                datalen = allocationLength;

            memcpy(&pGadget->m_InBuffer, &DeviceIdentificationReply, sizeof(DeviceIdentificationReply));
            pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                       pGadget->m_InBuffer, datalen);
            pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            pGadget->m_nnumber_blocks = 0;
            pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
            break;
        }
        default:
            MLOGNOTE("ScsiCommandDispatcher::HandleInquiry", "Inquiry (Unsupported Page)");
            pGadget->m_nnumber_blocks = 0;
            pGadget->setSenseData(0x05, 0x24, 0x00);
            pGadget->sendCheckCondition();
            break;
        }
    }
}

void ScsiCommandDispatcher::HandleStartStopUnit(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    int start = pCBW->CBWCB[4] & 1;
    int loej = (pCBW->CBWCB[4] >> 1) & 1;
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleStartStopUnit", "start/stop, start = %d, loej = %d", start, loej);
    pGadget->sendGoodStatus();
}

void ScsiCommandDispatcher::HandlePreventAllowMediumRemoval(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    pGadget->sendGoodStatus();
}

void ScsiCommandDispatcher::HandleReadCapacity10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    pGadget->m_ReadCapReply.nLastBlockAddr = htonl(GetLeadoutLBA(pGadget) - 1);
    memcpy(&pGadget->m_InBuffer, &pGadget->m_ReadCapReply, SIZE_READCAPREP);
    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, SIZE_READCAPREP);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleRead10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    if (pGadget->m_CDReady)
    {
        pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
        pGadget->m_nblock_address = (u32)(pCBW->CBWCB[2] << 24) | (u32)(pCBW->CBWCB[3] << 16) | (u32)(pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
        pGadget->m_nnumber_blocks = (u32)((pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8]);
        pGadget->transfer_block_size = 2048;
        pGadget->block_size = pGadget->data_block_size;
        pGadget->skip_bytes = pGadget->data_skip_bytes;
        pGadget->mcs = 0;
        pGadget->m_nbyteCount = pCBW->dCBWDataTransferLength;
        if (pGadget->m_nnumber_blocks == 0)
        {
            pGadget->m_nnumber_blocks = 1 + (pGadget->m_nbyteCount) / 2048;
        }
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleRead10", "LBA=%u, cnt=%u", pGadget->m_nblock_address, pGadget->m_nnumber_blocks);

        pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
        pGadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
    }
    else
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleRead10", "failed, %s", pGadget->m_CDReady ? "ready" : "not ready");
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
    }
}

void ScsiCommandDispatcher::HandleReadCD(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    if (!pGadget->m_CDReady)
    {
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
        return;
    }

    int expectedSectorType = (pCBW->CBWCB[1] >> 2) & 0x07;
    pGadget->m_nblock_address = (pCBW->CBWCB[2] << 24) | (pCBW->CBWCB[3] << 16) |
                       (pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    pGadget->m_nnumber_blocks = (pCBW->CBWCB[6] << 16) | (pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8];
    pGadget->mcs = (pCBW->CBWCB[9] >> 3) & 0x1F;

    CUETrackInfo trackInfo = GetTrackInfoForLBA(pGadget, pGadget->m_nblock_address);

    if (expectedSectorType != 0)
    {
        bool sector_type_ok = false;

        if (expectedSectorType == 1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            sector_type_ok = true;
        }
        else if (expectedSectorType == 2 &&
                 (trackInfo.track_mode == CUETrack_MODE1_2048 ||
                  trackInfo.track_mode == CUETrack_MODE1_2352))
        {
            sector_type_ok = true;
        }
        else if (expectedSectorType == 3 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true;
        }
        else if (expectedSectorType == 4 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true;
        }
        else if (expectedSectorType == 5 && trackInfo.track_mode == CUETrack_MODE2_2352)
        {
            sector_type_ok = true;
        }

        if (!sector_type_ok)
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadCD",
                            "READ CD: Sector type mismatch. Expected=%d, Track mode=%d",
                            expectedSectorType, trackInfo.track_mode);
            pGadget->setSenseData(0x05, 0x64, 0x00);
            pGadget->sendCheckCondition();
            return;
        }
    }

    u64 readEnd = (u64)pGadget->m_nblock_address * trackInfo.sector_length +
                  (u64)pGadget->m_nnumber_blocks * trackInfo.sector_length;
    if (readEnd > pGadget->m_pDevice->GetSize())
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleReadCD",
                 "READ CD: Read exceeds image size");
        pGadget->setSenseData(0x05, 0x21, 0x00);
        pGadget->sendCheckCondition();
        return;
    }
    switch (expectedSectorType)
    {
    case 0x01:
        pGadget->block_size = 2352;
        pGadget->transfer_block_size = 2352;
        pGadget->skip_bytes = 0;
        break;
    case 0x02:
        pGadget->skip_bytes = GetSkipbytesForTrack(trackInfo);
        pGadget->block_size = GetBlocksizeForTrack(trackInfo);
        pGadget->transfer_block_size = 2048;
        break;
    case 0x03:
        pGadget->skip_bytes = 16;
        pGadget->block_size = 2352;
        pGadget->transfer_block_size = 2336;
        break;
    case 0x04:
        pGadget->skip_bytes = GetSkipbytesForTrack(trackInfo);
        pGadget->block_size = GetBlocksizeForTrack(trackInfo);
        pGadget->transfer_block_size = 2048;
        break;
    case 0x05:
        pGadget->block_size = 2352;
        pGadget->skip_bytes = 24;
        pGadget->transfer_block_size = 2328;
        break;
    case 0x00:
    default:
        if (trackInfo.track_mode == CUETrack_AUDIO)
        {
            pGadget->block_size = 2352;
            pGadget->transfer_block_size = 2352;
            pGadget->skip_bytes = 0;
        }
        else
        {
            pGadget->block_size = GetBlocksizeForTrack(trackInfo);
            pGadget->transfer_block_size = pGadget->GetSectorLengthFromMCS(pGadget->mcs);
            pGadget->skip_bytes = pGadget->GetSkipBytesFromMCS(pGadget->mcs);
        }
        break;
    }

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadCD",
                    "READ CD: USB=%s, LBA=%u, blocks=%u, type=0x%02x, MCS=0x%02x",
                    pGadget->m_IsFullSpeed ? "FS" : "HS",
                    pGadget->m_nblock_address, pGadget->m_nnumber_blocks,
                    expectedSectorType, pGadget->mcs);

    pGadget->m_nbyteCount = pCBW->dCBWDataTransferLength;
    if (pGadget->m_nnumber_blocks == 0)
    {
        pGadget->m_nnumber_blocks = 1 + (pGadget->m_nbyteCount) / pGadget->transfer_block_size;
    }

    pGadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandleSetCDSpeed(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleVerify(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleReadTOC(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    if (!pGadget->m_CDReady)
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleReadTOC", "FAILED - CD not ready");
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
        return;
    }

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "CMD bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    pCBW->CBWCB[0], pCBW->CBWCB[1], pCBW->CBWCB[2], pCBW->CBWCB[3],
                    pCBW->CBWCB[4], pCBW->CBWCB[5], pCBW->CBWCB[6], pCBW->CBWCB[7],
                    pCBW->CBWCB[8], pCBW->CBWCB[9]);

    bool msf = (pCBW->CBWCB[1] >> 1) & 0x01;
    int format = pCBW->CBWCB[2] & 0x0F;
    int startingTrack = pCBW->CBWCB[6];
    int allocationLength = (pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8];

    bool useBCD = false;
    if (format == 0 && pCBW->CBWCB[9] == 0x80)
    {
        format = 2;
        useBCD = true;
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "Matshita vendor extension: Full TOC with BCD");
    }

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "Format=%d MSF=%d StartTrack=%d AllocLen=%d Control=0x%02x",
                    format, msf, startingTrack, allocationLength, pCBW->CBWCB[9]);

    switch (format)
    {
    case 0:
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "Format 0x00: Standard TOC");
        pGadget->DoReadTOC(msf, startingTrack, allocationLength);
        break;
    case 1:
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "Format 0x01: Session Info");
        pGadget->DoReadSessionInfo(msf, allocationLength);
        break;
    case 2:
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "Format 0x02: Full TOC (useBCD=%d)", useBCD);
        pGadget->DoReadFullTOC(startingTrack, allocationLength, useBCD);
        break;
    default:
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTOC", "INVALID FORMAT 0x%02x", format);
        pGadget->setSenseData(0x05, 0x24, 0x00);
        pGadget->sendCheckCondition();
    }
}

void ScsiCommandDispatcher::HandleReadSubChannel(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    unsigned int msf = (pCBW->CBWCB[1] >> 1) & 0x01;
    unsigned int parameter_list = pCBW->CBWCB[3];
    int allocationLength = (pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8];
    int length = 0;

    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));

    if (parameter_list == 0x00)
        parameter_list = 0x01;

    switch (parameter_list)
    {
    case 0x01:
    {
        TUSBCDSubChannelHeaderReply header;
        memset(&header, 0, SIZE_SUBCHANNEL_HEADER_REPLY);
        header.audioStatus = 0x15;
        header.dataLength = SIZE_SUBCHANNEL_01_DATA_REPLY;

        if (cdplayer)
        {
            unsigned int state = cdplayer->GetState();
            switch (state)
            {
            case CCDPlayer::PLAYING:
                header.audioStatus = 0x11;
                break;
            case CCDPlayer::PAUSED:
                header.audioStatus = 0x12;
                break;
            case CCDPlayer::STOPPED_OK:
                header.audioStatus = 0x13;
                break;
            case CCDPlayer::STOPPED_ERROR:
                header.audioStatus = 0x14;
                break;
            default:
                header.audioStatus = 0x15;
                break;
            }
        }

        TUSBCDSubChannel01CurrentPositionReply data;
        memset(&data, 0, SIZE_SUBCHANNEL_01_DATA_REPLY);
        data.dataFormatCode = 0x01;

        u32 address = 0;
        if (cdplayer)
        {
            address = cdplayer->GetCurrentAddress();
            data.absoluteAddress = GetAddress(address, msf, false);
            CUETrackInfo trackInfo = GetTrackInfoForLBA(pGadget, address);
            if (trackInfo.track_number != -1)
            {
                data.trackNumber = trackInfo.track_number;
                data.indexNumber = 0x01;
                data.relativeAddress = GetAddress(address - trackInfo.track_start, msf, true);
            }
        }

        length = SIZE_SUBCHANNEL_HEADER_REPLY + SIZE_SUBCHANNEL_01_DATA_REPLY;
        memcpy(pGadget->m_InBuffer, &header, SIZE_SUBCHANNEL_HEADER_REPLY);
        memcpy(pGadget->m_InBuffer + SIZE_SUBCHANNEL_HEADER_REPLY, &data, SIZE_SUBCHANNEL_01_DATA_REPLY);
        break;
    }
    case 0x02:
    case 0x03:
    default:
        break;
    }

    if (allocationLength < length)
        length = allocationLength;

    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, length);

    pGadget->m_nnumber_blocks = 0;
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleReadTrackInformation(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u8 addressType = pCBW->CBWCB[1] & 0x03;
    u32 address = (pCBW->CBWCB[2] << 24) | (pCBW->CBWCB[3] << 16) |
                  (pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    u16 allocationLength = (pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8];

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadTrackInformation",
                    "Read Track Information type=%d, addr=%u", addressType, address);

    if (!pGadget->m_CDReady)
    {
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
        return;
    }

    pGadget->DoReadTrackInformation(addressType, address, allocationLength);
}

void ScsiCommandDispatcher::HandleGetEventStatusNotification(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u8 polled = pCBW->CBWCB[1] & 0x01;
    u8 notificationClass = pCBW->CBWCB[4];
    u16 allocationLength = pCBW->CBWCB[7] << 8 | (pCBW->CBWCB[8]);

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetEventStatusNotification", "Get Event Status Notification");

    if (polled == 0)
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleGetEventStatusNotification", "Get Event Status Notification - we don't support async notifications");
        pGadget->setSenseData(0x05, 0x24, 0x00);
        pGadget->sendCheckCondition();
        return;
    }

    int length = 0;
    TUSBCDEventStatusReplyHeader header;
    memset(&header, 0, sizeof(header));
    header.supportedEventClass = 0x10;

    if (notificationClass & (1 << 4))
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleGetEventStatusNotification", "Get Event Status Notification - media change event response");
        header.eventDataLength = htons(0x04);
        header.notificationClass = 0x04;

        TUSBCDEventStatusReplyEvent event;
        memset(&event, 0, sizeof(event));

        if (pGadget->discChanged)
        {
            MLOGNOTE("ScsiCommandDispatcher::HandleGetEventStatusNotification", "Get Event Status Notification - sending NewMedia event");
            event.eventCode = 0x02;
            event.data[0] = pGadget->m_CDReady ? 0x02 : 0x00;
            if (allocationLength >= (sizeof(TUSBCDEventStatusReplyHeader) + sizeof(TUSBCDEventStatusReplyEvent)))
            {
                pGadget->discChanged = false;
            }
        }
        else if (pGadget->m_CDReady)
        {
            event.eventCode = 0x00;
            event.data[0] = 0x02;
        }
        else
        {
            event.eventCode = 0x03;
            event.data[0] = 0x00;
        }

        event.data[1] = 0x00;
        event.data[2] = 0x00;
        memcpy(pGadget->m_InBuffer + sizeof(TUSBCDEventStatusReplyHeader), &event, sizeof(TUSBCDEventStatusReplyEvent));
        length += sizeof(TUSBCDEventStatusReplyEvent);
    }
    else
    {
        MLOGNOTE("ScsiCommandDispatcher::HandleGetEventStatusNotification", "Get Event Status Notification - no supported class requested");
        header.notificationClass = 0x00;
        header.eventDataLength = htons(0x00);
    }

    memcpy(pGadget->m_InBuffer, &header, sizeof(TUSBCDEventStatusReplyHeader));
    length += sizeof(TUSBCDEventStatusReplyHeader);

    if (allocationLength < length)
        length = allocationLength;

    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandleReadDiscStructure(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u8 format = pCBW->CBWCB[7];
    u16 allocationLength = pCBW->CBWCB[8] << 8 | (pCBW->CBWCB[9]);
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadDiscStructure", "Read Disc Structure, format=0x%02x, allocation length is %lu, mediaType=%d", format, allocationLength, (int)pGadget->m_mediaType);

    if (pGadget->m_mediaType != MEDIA_TYPE::DVD &&
        (format == 0x00 || format == 0x02 || format == 0x03 || format == 0x04))
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadDiscStructure", "READ DISC STRUCTURE format 0x%02x for CD media - returning minimal response", format);
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
        header.dataLength = __builtin_bswap16(2);

        int length = sizeof(TUSBCDReadDiscStructureHeader);
        if (allocationLength < length)
            length = allocationLength;

        memcpy(pGadget->m_InBuffer, &header, length);
        pGadget->m_nnumber_blocks = 0;
        pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
        pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
        pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        return;
    }

    int length = 0;
    switch (format)
    {
    case 0x00:
    case 0x02:
    case 0x03:
    case 0x04:
    {
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
        header.dataLength = 2;
        memcpy(pGadget->m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
        length += sizeof(TUSBCDReadDiscStructureHeader);
        break;
    }
    case 0x01:
    {
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
        header.dataLength = 6;
        memcpy(pGadget->m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
        length += sizeof(TUSBCDReadDiscStructureHeader);

        u8 payload[] = {
            0x00, 0x00, 0x00, 0x00
        };
        memcpy(pGadget->m_InBuffer + sizeof(TUSBCDReadDiscStructureHeader), &payload, sizeof(payload));
        length += sizeof(payload);
        break;
    }
    default:
    {
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(TUSBCDReadDiscStructureHeader));
        header.dataLength = 2;
        memcpy(pGadget->m_InBuffer, &header, sizeof(TUSBCDReadDiscStructureHeader));
        length += sizeof(TUSBCDReadDiscStructureHeader);
        break;
    }
    }

    if (allocationLength < length)
        length = allocationLength;

    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleReadDiscInformation(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadDiscInformation", "Read Disc Information");

    pGadget->m_DiscInfoReply.disc_status = 0x0E;
    pGadget->m_DiscInfoReply.first_track_number = 0x01;
    pGadget->m_DiscInfoReply.number_of_sessions = 0x01;
    pGadget->m_DiscInfoReply.first_track_last_session = 0x01;
    pGadget->m_DiscInfoReply.last_track_last_session = GetLastTrackNumber(pGadget);

    CUETrackInfo trackInfo = GetTrackInfoForTrack(pGadget, 1);
    if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
    {
        pGadget->m_DiscInfoReply.disc_type = 0x00;
    }
    else
    {
        pGadget->m_DiscInfoReply.disc_type = 0x10;
    }

    u32 leadoutLBA = GetLeadoutLBA(pGadget);
    pGadget->m_DiscInfoReply.last_lead_in_start_time = htonl(leadoutLBA);
    pGadget->m_DiscInfoReply.last_possible_lead_out = htonl(leadoutLBA);

    u16 allocationLength = pCBW->CBWCB[7] << 8 | (pCBW->CBWCB[8]);
    int length = sizeof(TUSBDiscInfoReply);
    if (allocationLength < length)
        length = allocationLength;

    memcpy(pGadget->m_InBuffer, &pGadget->m_DiscInfoReply, length);
    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleReadHeader(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    bool MSF = (pCBW->CBWCB[1] & 0x02);
    uint32_t lba = (pCBW->CBWCB[2] << 24) | (pCBW->CBWCB[3] << 16) |
                   (pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    uint16_t allocationLength = (pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8];

    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleReadHeader",
                    "Read Header lba=%u, MSF=%d", lba, MSF);

    if (!pGadget->m_CDReady)
    {
        pGadget->setSenseData(0x02, 0x04, 0x00);
        pGadget->sendCheckCondition();
        return;
    }

    pGadget->DoReadHeader(MSF, lba, allocationLength);
}

void ScsiCommandDispatcher::HandleGetConfiguration(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    int rt = pCBW->CBWCB[1] & 0x03;
    int feature = (pCBW->CBWCB[2] << 8) | pCBW->CBWCB[3];
    u16 allocationLength = pCBW->CBWCB[7] << 8 | (pCBW->CBWCB[8]);
    int dataLength = 0;
    switch (rt)
    {
    case 0x00:
    case 0x01:
    {
        dataLength += sizeof(pGadget->header);

        TUSBCDProfileListFeatureReply dynProfileList = pGadget->profile_list;

        if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            dynProfileList.AdditionalLength = 0x08;
            memcpy(pGadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
            dataLength += sizeof(dynProfileList);
            TUSBCProfileDescriptorReply activeDVD = pGadget->dvd_profile;
            activeDVD.currentP = 0x01;
            memcpy(pGadget->m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
            dataLength += sizeof(activeDVD);
            TUSBCProfileDescriptorReply activeCD = pGadget->cdrom_profile;
            activeCD.currentP = 0x00;
            memcpy(pGadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
            dataLength += sizeof(activeCD);

            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION: DVD/CD combo drive, DVD current");
        }
        else
        {
            dynProfileList.AdditionalLength = 0x04;
            memcpy(pGadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
            dataLength += sizeof(dynProfileList);
            TUSBCProfileDescriptorReply activeCD = pGadget->cdrom_profile;
            activeCD.currentP = 0x01;
            memcpy(pGadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
            dataLength += sizeof(activeCD);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION: CD-ROM only drive");
        }

        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->core, sizeof(pGadget->core));
        dataLength += sizeof(pGadget->core);
        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->morphing, sizeof(pGadget->morphing));
        dataLength += sizeof(pGadget->morphing);
        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->mechanism, sizeof(pGadget->mechanism));
        dataLength += sizeof(pGadget->mechanism);
        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->multiread, sizeof(pGadget->multiread));
        dataLength += sizeof(pGadget->multiread);

        if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->dvdread, sizeof(pGadget->dvdread));
            dataLength += sizeof(pGadget->dvdread);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x%02x): Sending DVD-Read feature (0x001f)", rt);
        }
        else
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->cdread, sizeof(pGadget->cdread));
            dataLength += sizeof(pGadget->cdread);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x%02x): Sending CD-Read feature (0x001e), mediaType=%d", rt, (int)pGadget->m_mediaType);
        }

        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->powermanagement, sizeof(pGadget->powermanagement));
        dataLength += sizeof(pGadget->powermanagement);
        memcpy(pGadget->m_InBuffer + dataLength, &pGadget->audioplay, sizeof(pGadget->audioplay));
        dataLength += sizeof(pGadget->audioplay);
        TUSBCDFeatureHeaderReply dynHeader = pGadget->header;
        if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_DVD_ROM (0x0010)", rt);
        }
        else
        {
            dynHeader.currentProfile = htons(PROFILE_CDROM);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x%02x): Returning PROFILE_CDROM (0x0008)", rt);
        }
        dynHeader.dataLength = htonl(dataLength - 4);
        memcpy(pGadget->m_InBuffer, &dynHeader, sizeof(dynHeader));
        break;
    }
    case 0x02:
    {
        dataLength += sizeof(pGadget->header);
        switch (feature)
        {
        case 0x00:
        {
            TUSBCDProfileListFeatureReply dynProfileList = pGadget->profile_list;

            if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
            {
                dynProfileList.AdditionalLength = 0x08;
                memcpy(pGadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);
                TUSBCProfileDescriptorReply activeDVD = pGadget->dvd_profile;
                activeDVD.currentP = 0x01;
                memcpy(pGadget->m_InBuffer + dataLength, &activeDVD, sizeof(activeDVD));
                dataLength += sizeof(activeDVD);
                TUSBCProfileDescriptorReply activeCD = pGadget->cdrom_profile;
                activeCD.currentP = 0x00;
                memcpy(pGadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);
                CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x00): DVD/CD combo, DVD current");
            }
            else
            {
                dynProfileList.AdditionalLength = 0x04;
                memcpy(pGadget->m_InBuffer + dataLength, &dynProfileList, sizeof(dynProfileList));
                dataLength += sizeof(dynProfileList);
                TUSBCProfileDescriptorReply activeCD = pGadget->cdrom_profile;
                activeCD.currentP = 0x01;
                memcpy(pGadget->m_InBuffer + dataLength, &activeCD, sizeof(activeCD));
                dataLength += sizeof(activeCD);
                CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x02, feat 0x00): CD-ROM only drive (profile 0x0008, current=%d, length=0x%02x)",
                                activeCD.currentP, dynProfileList.AdditionalLength);
            }
            break;
        }
        case 0x01:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->core, sizeof(pGadget->core));
            dataLength += sizeof(pGadget->core);
            break;
        }
        case 0x02:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->morphing, sizeof(pGadget->morphing));
            dataLength += sizeof(pGadget->morphing);
            break;
        }
        case 0x03:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->mechanism, sizeof(pGadget->mechanism));
            dataLength += sizeof(pGadget->mechanism);
            break;
        }
        case 0x1d:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->multiread, sizeof(pGadget->multiread));
            dataLength += sizeof(pGadget->multiread);
            break;
        }
        case 0x1e:
        {
            if (pGadget->m_mediaType == MEDIA_TYPE::CD)
            {
                memcpy(pGadget->m_InBuffer + dataLength, &pGadget->cdread, sizeof(pGadget->cdread));
                dataLength += sizeof(pGadget->cdread);
            }
            break;
        }
        case 0x1f:
        {
            if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
            {
                memcpy(pGadget->m_InBuffer + dataLength, &pGadget->dvdread, sizeof(pGadget->dvdread));
                dataLength += sizeof(pGadget->dvdread);
            }
            break;
        }
        case 0x100:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->powermanagement, sizeof(pGadget->powermanagement));
            dataLength += sizeof(pGadget->powermanagement);
            break;
        }
        case 0x103:
        {
            memcpy(pGadget->m_InBuffer + dataLength, &pGadget->audioplay, sizeof(pGadget->audioplay));
            dataLength += sizeof(pGadget->audioplay);
            break;
        }
        default:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x02): Unhandled feature 0x%04x requested", feature);
            break;
        }
        }
        TUSBCDFeatureHeaderReply dynHeader = pGadget->header;
        if (pGadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            dynHeader.currentProfile = htons(PROFILE_DVD_ROM);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x02): Returning PROFILE_DVD_ROM (0x0010)");
        }
        else
        {
            dynHeader.currentProfile = htons(PROFILE_CDROM);
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleGetConfiguration", "GET CONFIGURATION (rt 0x02): Returning PROFILE_CDROM (0x0008)");
        }
        dynHeader.dataLength = htonl(dataLength - 4);
        memcpy(pGadget->m_InBuffer, &dynHeader, sizeof(dynHeader));
        break;
    }
    }

    if (allocationLength < dataLength)
        dataLength = allocationLength;

    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, dataLength);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandlePauseResume(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandlePauseResume", "PAUSE/RESUME");
    int resume = pCBW->CBWCB[8] & 0x01;
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        if (resume)
            cdplayer->Resume();
        else
            cdplayer->Pause();
    }
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleSeek(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    pGadget->m_nblock_address = (u32)(pCBW->CBWCB[2] << 24) | (u32)(pCBW->CBWCB[3] << 16) | (u32)(pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleSeek", "SEEK to LBA %lu", pGadget->m_nblock_address);
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Seek(pGadget->m_nblock_address);
    }
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandlePlayAudioMSF(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u8 SM = pCBW->CBWCB[3];
    u8 SS = pCBW->CBWCB[4];
    u8 SF = pCBW->CBWCB[5];
    u8 EM = pCBW->CBWCB[6];
    u8 ES = pCBW->CBWCB[7];
    u8 EF = pCBW->CBWCB[8];

    u32 start_lba = msf_to_lba(SM, SS, SF);
    u32 end_lba = msf_to_lba(EM, ES, EF);
    int num_blocks = end_lba - start_lba;
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudioMSF", "PLAY AUDIO MSF. Start MSF %d:%d:%d, End MSF: %d:%d:%d, start LBA %u, end LBA %u", SM, SS, SF, EM, ES, EF, start_lba, end_lba);

    CUETrackInfo trackInfo = GetTrackInfoForLBA(pGadget, start_lba);
    if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
    {
        CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudioMSF", "CD Player found, sending command");
        CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
        if (cdplayer)
        {
            if (start_lba == 0xFFFFFFFF)
            {
                CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudioMSF", "CD Player found, Resume");
                cdplayer->Resume();
            }
            else if (start_lba == end_lba)
            {
                CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudioMSF", "CD Player found, Pause");
                cdplayer->Pause();
            }
            else
            {
                CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudioMSF", "CD Player found, Play");
                cdplayer->Play(start_lba, num_blocks);
            }
        }
    }
    else
    {
        MLOGNOTE("ScsiCommandDispatcher::HandlePlayAudioMSF", "PLAY AUDIO MSF: Not an audio track");
        pGadget->bmCSWStatus = CD_CSW_STATUS_FAIL;
        pGadget->m_SenseParams.bSenseKey = 0x05;
        pGadget->m_SenseParams.bAddlSenseCode = 0x64;
        pGadget->m_SenseParams.bAddlSenseCodeQual = 0x00;
    }

    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleStopScan(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleStopScan", "STOP / SCAN");
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Pause();
    }
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandlePlayAudio10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandlePlayAudio10", "PLAY AUDIO (10)");
    pGadget->m_nblock_address = (u32)(pCBW->CBWCB[2] << 24) | (u32)(pCBW->CBWCB[3] << 16) | (u32)(pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    pGadget->m_nnumber_blocks = (u32)((pCBW->CBWCB[7] << 8) | pCBW->CBWCB[8]);
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudio10", "PLAY AUDIO (10) Playing from %lu for %lu blocks", pGadget->m_nblock_address, pGadget->m_nnumber_blocks);

    if (pGadget->m_nnumber_blocks > 0)
    {
        CUETrackInfo trackInfo = GetTrackInfoForLBA(pGadget, pGadget->m_nblock_address);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer)
            {
                MLOGNOTE("ScsiCommandDispatcher::HandlePlayAudio10", "PLAY AUDIO (10) Play command sent");
                if (pGadget->m_nblock_address == 0xffffffff)
                    cdplayer->Resume();
                else
                    cdplayer->Play(pGadget->m_nblock_address, pGadget->m_nnumber_blocks);
            }
        }
        else
        {
            pGadget->bmCSWStatus = CD_CSW_STATUS_FAIL;
            pGadget->m_SenseParams.bSenseKey = 0x05;
            pGadget->m_SenseParams.bAddlSenseCode = 0x64;
            pGadget->m_SenseParams.bAddlSenseCodeQual = 0x00;
        }
    }

    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandlePlayAudio12(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandlePlayAudio12", "PLAY AUDIO (12)");
    pGadget->m_nblock_address = (u32)(pCBW->CBWCB[2] << 24) | (u32)(pCBW->CBWCB[3] << 16) | (u32)(pCBW->CBWCB[4] << 8) | pCBW->CBWCB[5];
    pGadget->m_nnumber_blocks = (u32)(pCBW->CBWCB[6] << 24) | (u32)(pCBW->CBWCB[7] << 16) | (u32)(pCBW->CBWCB[8] << 8) | pCBW->CBWCB[9];
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandlePlayAudio12", "PLAY AUDIO (12) Playing from %lu for %lu blocks", pGadget->m_nblock_address, pGadget->m_nnumber_blocks);

    if (pGadget->m_nnumber_blocks > 0)
    {
        CUETrackInfo trackInfo = GetTrackInfoForLBA(pGadget, pGadget->m_nblock_address);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
        {
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer)
            {
                MLOGNOTE("ScsiCommandDispatcher::HandlePlayAudio12", "PLAY AUDIO (12) Play command sent");
                if (pGadget->m_nblock_address == 0xffffffff)
                    cdplayer->Resume();
                else
                    cdplayer->Play(pGadget->m_nblock_address, pGadget->m_nnumber_blocks);
            }
        }
        else
        {
            pGadget->bmCSWStatus = CD_CSW_STATUS_FAIL;
            pGadget->m_SenseParams.bSenseKey = 0x05;
            pGadget->m_SenseParams.bAddlSenseCode = 0x64;
            pGadget->m_SenseParams.bAddlSenseCodeQual = 0x00;
        }
    }

    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleModeSelect10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    u16 transferLength = pCBW->CBWCB[7] << 8 | (pCBW->CBWCB[8]);
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSelect10", "Mode Select (10), transferLength is %u", transferLength);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataOut;
    pGadget->m_pEP[CUSBCDGadget::EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataOut,
                                pGadget->m_OutBuffer, transferLength);
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleModeSense6(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6)");
    int page_control = (pCBW->CBWCB[2] >> 6) & 0x03;
    int page = pCBW->CBWCB[2] & 0x3f;
    int allocationLength = pCBW->CBWCB[4];
    int length = 0;

    if (page_control == 0x03)
    {
        pGadget->bmCSWStatus = CD_CSW_STATUS_FAIL;
        pGadget->m_SenseParams.bSenseKey = 0x05;
        pGadget->m_SenseParams.bAddlSenseCode = 0x39;
        pGadget->m_SenseParams.bAddlSenseCodeQual = 0x00;
    }
    else
    {
        ModeSense6Header reply_header;
        memset(&reply_header, 0, sizeof(reply_header));
        reply_header.mediumType = GetMediumType(pGadget);

        switch (page)
        {
        case 0x3f:
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) 0x3f: All Mode Pages");
        case 0x01:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) 0x01 response");
            ModePage0x01Data codepage;
            memset(&codepage, 0, sizeof(codepage));
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x1a:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) 0x1a response");
            ModePage0x1AData codepage;
            memset(&codepage, 0, sizeof(codepage));
            codepage.pageCodeAndPS = 0x1a;
            codepage.pageLength = 0x0a;
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x2a:
        {
            ModePage0x2AData codepage;
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) 0x2a response");
            //FillModePage2A(codepage);
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x0e:
        {
            MLOGNOTE("ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) 0x0e response");
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            u8 volume = 0xff;
            if (cdplayer)
            {
                volume = 0xff;
            }
            ModePage0x0EData codepage;
            memset(&codepage, 0, sizeof(codepage));
            codepage.pageCodeAndPS = 0x0e;
            codepage.pageLength = 16;
            codepage.IMMEDAndSOTC = 0x05;
            codepage.CDDAOutput0Select = 0x01;
            codepage.Output0Volume = volume;
            codepage.CDDAOutput1Select = 0x02;
            codepage.Output1Volume = volume;
            codepage.CDDAOutput2Select = 0x00;
            codepage.Output2Volume = 0x00;
            codepage.CDDAOutput3Select = 0x00;
            codepage.Output3Volume = 0x00;
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            break;
        }
        default:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense6", "Mode Sense (6) unsupported page 0x%02x", page);
            pGadget->setSenseData(0x05, 0x24, 0x00);
            pGadget->sendCheckCondition();
            return;
        }
        }
        reply_header.modeDataLength = htons(length - 1);
        memcpy(pGadget->m_InBuffer, &reply_header, sizeof(reply_header));
    }
    if (allocationLength < length)
        length = allocationLength;
    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleModeSense10(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    int LLBAA = (pCBW->CBWCB[1] >> 7) & 0x01;
    int DBD = (pCBW->CBWCB[1] >> 6) & 0x01;
    int page = pCBW->CBWCB[2] & 0x3F;
    int page_control = (pCBW->CBWCB[2] >> 6) & 0x03;
    u16 allocationLength = pCBW->CBWCB[7] << 8 | (pCBW->CBWCB[8]);
    CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) with LLBAA = %d, DBD = %d, page = %02x, allocationLength = %lu", LLBAA, DBD, page, allocationLength);

    int length = 0;

    if (page_control == 0x03)
    {
        pGadget->bmCSWStatus = CD_CSW_STATUS_FAIL;
        pGadget->m_SenseParams.bSenseKey = 0x05;
        pGadget->m_SenseParams.bAddlSenseCode = 0x39;
        pGadget->m_SenseParams.bAddlSenseCodeQual = 0x00;
    }
    else
    {
        ModeSense10Header reply_header;
        memset(&reply_header, 0, sizeof(reply_header));
        reply_header.mediumType = GetMediumType(pGadget);
        length += sizeof(reply_header);

        switch (page)
        {
        case 0x3f:
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) 0x3f: All Mode Pages");
        case 0x01:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) 0x01 response");
            ModePage0x01Data codepage;
            memset(&codepage, 0, sizeof(codepage));
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x0D:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10",
                            "MODE SENSE(10) Page 0x0D (CD Device Parameters)");
            struct CDDeviceParametersPage
            {
                u8 pageCode;
                u8 pageLength;
                u8 reserved1;
                u8 inactivityTimer;
                u16 secondsPerMSF;
                u16 framesPerMSF;
            } PACKED;

            CDDeviceParametersPage codePage = {0};
            codePage.pageCode = 0x0D;
            codePage.pageLength = 0x06;
            codePage.inactivityTimer = 0x00;
            codePage.secondsPerMSF = htons(60);
            codePage.framesPerMSF = htons(75);
            memcpy(pGadget->m_InBuffer + length, &codePage, sizeof(codePage));
            length += sizeof(codePage);
            break;
        }
        case 0x1a:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) 0x1a response");
            ModePage0x1AData codepage;
            memset(&codepage, 0, sizeof(codepage));
            codepage.pageCodeAndPS = 0x1a;
            codepage.pageLength = 0x0a;
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x2a:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) 0x2a response");
            ModePage0x2AData codepage;
            //FillModePage2A(codepage);
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            if (page != 0x3f)
                break;
        }
        case 0x0e:
        {
            CDROM_DEBUG_LOG(pGadget, "ScsiCommandDispatcher::HandleModeSense10", "Mode Sense (10) 0x0e response");
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            u8 volume = 0xff;
            if (cdplayer)
            {
                volume = 0xff;
            }
            ModePage0x0EData codepage;
            memset(&codepage, 0, sizeof(codepage));
            codepage.pageCodeAndPS = 0x0e;
            codepage.pageLength = 16;
            codepage.IMMEDAndSOTC = 0x05;
            codepage.CDDAOutput0Select = 0x01;
            codepage.Output0Volume = volume;
            codepage.CDDAOutput1Select = 0x02;
            codepage.Output1Volume = volume;
            codepage.CDDAOutput2Select = 0x00;
            codepage.Output2Volume = 0x00;
            codepage.CDDAOutput3Select = 0x00;
            codepage.Output3Volume = 0x00;
            memcpy(pGadget->m_InBuffer + length, &codepage, sizeof(codepage));
            length += sizeof(codepage);
            break;
        }
        default:
        {
            pGadget->setSenseData(0x05, 0x24, 0x00);
            pGadget->sendCheckCondition();
            return;
        }
        }
        reply_header.modeDataLength = htons(length - 2);
        memcpy(pGadget->m_InBuffer, &reply_header, sizeof(reply_header));
    }

    if (allocationLength < length)
        length = allocationLength;

    pGadget->m_nnumber_blocks = 0;
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, pGadget->m_InBuffer, length);
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleGetPerformance(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleGetPerformance", "GET PERFORMANCE (0xAC)");
    u8 getPerformanceStub[20] = {
        0x00, 0x00, 0x00, 0x10,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00
    };
    memcpy(pGadget->m_InBuffer, getPerformanceStub, sizeof(getPerformanceStub));
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, sizeof(getPerformanceStub));
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = pGadget->bmCSWStatus;
}

void ScsiCommandDispatcher::HandleA4(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleA4", "A4 from Win2k");
    u8 response[] = {0x0, 0x6, 0x0, 0x0, 0x25, 0xff, 0x1, 0x0};
    memcpy(pGadget->m_InBuffer, response, sizeof(response));
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, sizeof(response));
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandleListDevices(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleListDevices", "SCSITB List Devices");
    u8 devices[] = {0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    memcpy(pGadget->m_InBuffer, devices, sizeof(devices));
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, sizeof(devices));
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandleNumberOfFiles(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleNumberOfFiles", "SCSITB Number of Files/CDs");
    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    const size_t MAX_ENTRIES = 100;
    size_t count = scsitbservice->GetCount();
    if (count > MAX_ENTRIES)
        count = MAX_ENTRIES;
    u8 num = (u8)count;
    MLOGNOTE("ScsiCommandDispatcher::HandleNumberOfFiles", "SCSITB Discovered %d Files/CDs", num);
    memcpy(pGadget->m_InBuffer, &num, sizeof(num));
    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, sizeof(num));
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void ScsiCommandDispatcher::HandleListFiles(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("ScsiCommandDispatcher::HandleListFiles", "SCSITB List Files/CDs");
    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    const size_t MAX_ENTRIES = 100;
    size_t count = scsitbservice->GetCount();
    if (count > MAX_ENTRIES)
        count = MAX_ENTRIES;

    TUSBCDToolboxFileEntry* entries = new TUSBCDToolboxFileEntry[MAX_ENTRIES];
    for (u8 i = 0; i < count; ++i)
    {
        TUSBCDToolboxFileEntry* entry = &entries[i];
        entry->index = i;
        entry->type = 0;
        const char* name = scsitbservice->GetName(i);
        size_t j = 0;
        for (; j < 32 && name[j] != '\0'; ++j)
        {
            entry->name[j] = (u8)name[j];
        }
        entry->name[j] = 0;
        //DWORD size = scsitbservice->GetSize(i);
        //entry->size[0] = 0;
        //entry->size[1] = (size >> 24) & 0xFF;
        //entry->size[2] = (size >> 16) & 0xFF;
        //entry->size[3] = (size >> 8) & 0xFF;
        //entry->size[4] = size & 0xFF;
    }

    memcpy(pGadget->m_InBuffer, entries, count * sizeof(TUSBCDToolboxFileEntry));

    pGadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                               pGadget->m_InBuffer, count * sizeof(TUSBCDToolboxFileEntry));
    pGadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    delete[] entries;
}

void ScsiCommandDispatcher::HandleSetNextCD(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    int index = pCBW->CBWCB[1];
    MLOGNOTE("ScsiCommandDispatcher::HandleSetNextCD", "SET NEXT CD index %d", index);
    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    scsitbservice->SetNextCD(index);
    pGadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    pGadget->SendCSW();
}

void ScsiCommandDispatcher::HandleUnknown(CUSBCDGadget* pGadget, const TUSBCDCBW* pCBW)
{
    MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Unknown SCSI Command is 0x%02x", pCBW->CBWCB[0]);
    pGadget->setSenseData(0x05, 0x20, 0x00); // INVALID COMMAND OPERATION CODE
    pGadget->sendCheckCondition();
}
