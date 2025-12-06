//
// scsi_toc.cpp
//
// SCSI TOC, Disc Info, Track Info
//
#include <usbcdgadget/scsi_toc.h>
#include <usbcdgadget/cd_utils.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <cdplayer/cdplayer.h>
#include <circle/util.h>

#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...) // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)

#define CDROM_DEBUG_LOG(From, ...)       \
    do                                   \
    {                                    \
        if (gadget->m_bDebugLogging)     \
            MLOGNOTE(From, __VA_ARGS__); \
    } while (0)

void SCSITOC::ReadTOC(CUSBCDGadget *gadget)
{
    if (!gadget->m_CDReady)
    {
        MLOGNOTE("SCSITOC::ReadTOC", "FAILED - CD not ready");
        gadget->setSenseData(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
        return;
    }

    // LOG FULL COMMAND BYTES
    CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "CMD bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    gadget->m_CBW.CBWCB[0], gadget->m_CBW.CBWCB[1], gadget->m_CBW.CBWCB[2], gadget->m_CBW.CBWCB[3],
                    gadget->m_CBW.CBWCB[4], gadget->m_CBW.CBWCB[5], gadget->m_CBW.CBWCB[6], gadget->m_CBW.CBWCB[7],
                    gadget->m_CBW.CBWCB[8], gadget->m_CBW.CBWCB[9]);

    bool msf = (gadget->m_CBW.CBWCB[1] >> 1) & 0x01;
    int format = gadget->m_CBW.CBWCB[2] & 0x0F;
    int startingTrack = gadget->m_CBW.CBWCB[6];
    int allocationLength = (gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8];

    // Check for vendor extension flags (Matshita compatibility)
    bool useBCD = false;
    if (format == 0 && gadget->m_CBW.CBWCB[9] == 0x80)
    {
        format = 2;
        useBCD = true;
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Matshita vendor extension: Full TOC with BCD");
    }

    CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Format=%d MSF=%d StartTrack=%d AllocLen=%d Control=0x%02x",
                    format, msf, startingTrack, allocationLength, gadget->m_CBW.CBWCB[9]);

    if (!gadget->m_CDReady)
    {
        MLOGNOTE("SCSITOC::ReadTOC", "FAILED - CD not ready");
        gadget->setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
        return;
    }

    switch (format)
    {
    case 0:
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Format 0x00: Standard TOC");
        DoReadTOC(gadget, msf, startingTrack, allocationLength);
        break;
    case 1:
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Format 0x01: Session Info");
        DoReadSessionInfo(gadget, msf, allocationLength);
        break;
    case 2:
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Format 0x02: Full TOC (useBCD=%d)", useBCD);
        DoReadFullTOC(gadget, startingTrack, allocationLength, useBCD);
        break;
    case 4:
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "Format 0x04: ATIP - returning minimal response");
        {
            // Minimal ATIP response indicating pressed disc (non-recordable)
            uint8_t atip[28];
            memset(atip, 0, sizeof(atip));
            atip[0] = 0x00;
            atip[1] = 0x1A; // Length = 26 bytes
            atip[2] = 0x00; // Reserved
            atip[3] = 0x00; // Reserved

            uint16_t len = sizeof(atip);
            if (len > allocationLength)
                len = allocationLength;

            memcpy(gadget->m_InBuffer, atip, len);
            gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, len);
            gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
            gadget->m_nnumber_blocks = 0;
            gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        }
        break;
    default:
        CDROM_DEBUG_LOG("SCSITOC::ReadTOC", "INVALID FORMAT 0x%02x", format);
        gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        gadget->sendCheckCondition();
    }
}

// Functions inspiried by bluescsi v2
// Helper function for TOC entry formatting
void SCSITOC::FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF)
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
        CDUtils::LBA2MSF(track->data_start, &dest[5], false);
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
void SCSITOC::FormatRawTOCEntry(CUSBCDGadget *gadget, const CUETrackInfo *track, uint8_t *dest, bool useBCD)
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
        CDUtils::LBA2MSFBCD(track->data_start, &dest[8], false);
    }
    else
    {
        CDUtils::LBA2MSF(track->data_start, &dest[8], false);
    }
}

// Complete READ TOC handler
void SCSITOC::DoReadTOC(CUSBCDGadget *gadget, bool msf, uint8_t startingTrack, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "Entry: msf=%d, startTrack=%d, allocLen=%d", msf, startingTrack, allocationLength);

    // NO SPECIAL CASE FOR 0xAA - let it flow through normally

    if (gadget->m_pDevice == nullptr)
    {
        MLOGDEBUG("TOC requested but no device present");
        gadget->sendCheckCondition();
        return;
    }

    // Format track info
    uint8_t *trackdata = &gadget->m_InBuffer[4];
    int trackcount = 0;
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};

    CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "Building track list");
    gadget->cueParser.restart();
    const CUETrackInfo *trackinfo;
    while ((trackinfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
            firsttrack = trackinfo->track_number;
        lasttrack = *trackinfo;

        // Include tracks >= startingTrack
        // Since 0xAA (170) is > any track number (1-99), this will SKIP all tracks when startingTrack=0xAA
        if (startingTrack == 0 || startingTrack <= trackinfo->track_number)
        {
            FormatTOCEntry(trackinfo, &trackdata[8 * trackcount], msf);

            CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "  Track %d: mode=%d, start=%u, msf=%d",
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
    leadout.data_start = CDUtils::GetLeadoutLBA(gadget);

    // Add leadout to the TOC
    FormatTOCEntry(&leadout, &trackdata[8 * trackcount], msf);

    CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "  Lead-out: LBA=%u", leadout.data_start);
    trackcount++;

    // Format header
    uint16_t toc_length = 2 + trackcount * 8;
    gadget->m_InBuffer[0] = (toc_length >> 8) & 0xFF;
    gadget->m_InBuffer[1] = toc_length & 0xFF;
    gadget->m_InBuffer[2] = firsttrack;
    gadget->m_InBuffer[3] = lasttrack.track_number;

    CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "Header: Length=%d, First=%d, Last=%d, Tracks=%d",
                    toc_length, firsttrack, lasttrack.track_number, trackcount);

    // Validation: when startingTrack is specified (not 0), we need at least the leadout
    if (startingTrack != 0 && startingTrack != 0xAA && trackcount < 2)
    {
        CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "INVALID: startTrack=%d but trackcount=%d", startingTrack, trackcount);
        gadget->setSenseData(0x05, 0x24, 0x00);
        gadget->sendCheckCondition();
        return;
    }

    uint32_t len = 2 + toc_length;
    if (len > allocationLength)
        len = allocationLength;

    // LOG RESPONSE BUFFER
    CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "Response (%d bytes, %d requested, full_size=%d):",
                    len, allocationLength, 2 + toc_length);
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, gadget->m_InBuffer[i + 0], gadget->m_InBuffer[i + 1], gadget->m_InBuffer[i + 2], gadget->m_InBuffer[i + 3],
                            gadget->m_InBuffer[i + 4], gadget->m_InBuffer[i + 5], gadget->m_InBuffer[i + 6], gadget->m_InBuffer[i + 7],
                            gadget->m_InBuffer[i + 8], gadget->m_InBuffer[i + 9], gadget->m_InBuffer[i + 10], gadget->m_InBuffer[i + 11],
                            gadget->m_InBuffer[i + 12], gadget->m_InBuffer[i + 13], gadget->m_InBuffer[i + 14], gadget->m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", gadget->m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG("SCSITOC::DoReadTOC", "%s", buf);
        }
    }

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, len);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_nnumber_blocks = 0;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSITOC::DoReadSessionInfo(CUSBCDGadget *gadget, bool msf, uint16_t allocationLength)
{
    CDROM_DEBUG_LOG("SCSITOC::DoReadSessionInfo", "Entry: msf=%d, allocLen=%d", msf, allocationLength);

    uint8_t sessionTOC[12] = {
        0x00, 0x0A, 0x01, 0x01, 0x00, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00};

    gadget->cueParser.restart();
    const CUETrackInfo *trackinfo = gadget->cueParser.next_track();
    if (trackinfo)
    {
        CDROM_DEBUG_LOG("SCSITOC::DoReadSessionInfo", "First track: num=%d, start=%u",
                        trackinfo->track_number, trackinfo->data_start);

        if (msf)
        {
            sessionTOC[8] = 0;
            CDUtils::LBA2MSF(trackinfo->data_start, &sessionTOC[9], false);
            CDROM_DEBUG_LOG("SCSITOC::DoReadSessionInfo", "MSF: %02x:%02x:%02x",
                            sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
        else
        {
            sessionTOC[8] = (trackinfo->data_start >> 24) & 0xFF;
            sessionTOC[9] = (trackinfo->data_start >> 16) & 0xFF;
            sessionTOC[10] = (trackinfo->data_start >> 8) & 0xFF;
            sessionTOC[11] = (trackinfo->data_start >> 0) & 0xFF;
            CDROM_DEBUG_LOG("SCSITOC::DoReadSessionInfo", "LBA bytes: %02x %02x %02x %02x",
                            sessionTOC[8], sessionTOC[9], sessionTOC[10], sessionTOC[11]);
        }
    }

    int len = sizeof(sessionTOC);
    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG("SCSITOC::DoReadSessionInfo", "Sending %d bytes", len);
    memcpy(gadget->m_InBuffer, sessionTOC, len);
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, len);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_nnumber_blocks = 0;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSITOC::DoReadFullTOC(CUSBCDGadget *gadget, uint8_t session, uint16_t allocationLength, bool useBCD)
{
    CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "Entry: session=%d, allocLen=%d, BCD=%d",
                    session, allocationLength, useBCD);

    if (gadget->m_pDevice == nullptr)
    {
        MLOGDEBUG("TOC requested but no device present");
        gadget->sendCheckCondition();
        return;
    }

    if (session > 1)
    {
        CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "INVALID SESSION %d", session);
        gadget->setSenseData(0x05, 0x24, 0x00);
        gadget->sendCheckCondition();
        return;
    }

    // Base full TOC structure with A0/A1/A2 descriptors
    uint8_t fullTOCBase[37] = {
        0x00, 0x2E, 0x01, 0x01,
        0x01, 0x14, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x14, 0x00, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t len = sizeof(fullTOCBase);
    memcpy(gadget->m_InBuffer, fullTOCBase, len);

    // Find first and last tracks
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};
    const CUETrackInfo *trackinfo;

    gadget->cueParser.restart();
    while ((trackinfo = gadget->cueParser.next_track()) != nullptr)
    {
        if (firsttrack < 0)
        {
            firsttrack = trackinfo->track_number;
            if (trackinfo->track_mode == CUETrack_AUDIO)
            {
                gadget->m_InBuffer[5] = 0x10;  // A0 control for audio
                gadget->m_InBuffer[16] = 0x10; // A1 control for audio
                gadget->m_InBuffer[27] = 0x10; // A2 control for audio
            }
            CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "First track: %d, mode=%d", firsttrack, trackinfo->track_mode);
        }
        lasttrack = *trackinfo;

        // Add track descriptor
        FormatRawTOCEntry(gadget, trackinfo, &gadget->m_InBuffer[len], useBCD);

        CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "  Track %d: mode=%d, start=%u",
                        trackinfo->track_number, trackinfo->track_mode, trackinfo->data_start);

        len += 11;
    }

    // Update A0, A1, A2 descriptors
    gadget->m_InBuffer[12] = firsttrack;
    gadget->m_InBuffer[23] = lasttrack.track_number;

    CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "Header: First=%d, Last=%d. A0: First=%d, A1: Last=%d",
                    firsttrack, lasttrack.track_number, firsttrack, lasttrack.track_number);

    // A2: Leadout position
    u32 leadoutLBA = CDUtils::GetLeadoutLBA(gadget);
    CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "A2: Lead-out LBA=%u", leadoutLBA);

    if (useBCD)
    {
        CDUtils::LBA2MSFBCD(leadoutLBA, &gadget->m_InBuffer[34], false);
        CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "A2 MSF (BCD): %02x:%02x:%02x",
                        gadget->m_InBuffer[34], gadget->m_InBuffer[35], gadget->m_InBuffer[36]);
    }
    else
    {
        CDUtils::LBA2MSF(leadoutLBA, &gadget->m_InBuffer[34], false);
        CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "A2 MSF: %02x:%02x:%02x",
                        gadget->m_InBuffer[34], gadget->m_InBuffer[35], gadget->m_InBuffer[36]);
    }

    // Update TOC length
    uint16_t toclen = len - 2;
    gadget->m_InBuffer[0] = (toclen >> 8) & 0xFF;
    gadget->m_InBuffer[1] = toclen & 0xFF;

    if (len > allocationLength)
        len = allocationLength;

    CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "Response: %d bytes (%d total, %d requested)",
                    len, toclen + 2, allocationLength);

    // LOG RESPONSE BUFFER
    for (uint32_t i = 0; i < len && i < 48; i += 16)
    {
        int remaining = (len - i < 16) ? len - i : 16;
        if (remaining >= 16)
        {
            CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "  [%02d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            i, gadget->m_InBuffer[i + 0], gadget->m_InBuffer[i + 1], gadget->m_InBuffer[i + 2], gadget->m_InBuffer[i + 3],
                            gadget->m_InBuffer[i + 4], gadget->m_InBuffer[i + 5], gadget->m_InBuffer[i + 6], gadget->m_InBuffer[i + 7],
                            gadget->m_InBuffer[i + 8], gadget->m_InBuffer[i + 9], gadget->m_InBuffer[i + 10], gadget->m_InBuffer[i + 11],
                            gadget->m_InBuffer[i + 12], gadget->m_InBuffer[i + 13], gadget->m_InBuffer[i + 14], gadget->m_InBuffer[i + 15]);
        }
        else
        {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "  [%02d] ", i);
            for (int j = 0; j < remaining; j++)
            {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", gadget->m_InBuffer[i + j]);
            }
            CDROM_DEBUG_LOG("SCSITOC::DoReadFullTOC", "%s", buf);
        }
    }

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, len);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_nnumber_blocks = 0;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSITOC::ReadDiscInformation(CUSBCDGadget *gadget)
{
    CDROM_DEBUG_LOG("SCSITOC::ReadDiscInformation", "Read Disc Information");

    // Update disc information with current media state (MacOS-compatible)
    gadget->m_DiscInfoReply.disc_status = 0x0E; // Complete disc, finalized (bits 1-0=10b), last session complete (bit 3)
    gadget->m_DiscInfoReply.first_track_number = 0x01;
    gadget->m_DiscInfoReply.number_of_sessions = 0x01; // Single session
    gadget->m_DiscInfoReply.first_track_last_session = 0x01;
    gadget->m_DiscInfoReply.last_track_last_session = CDUtils::GetLastTrackNumber(gadget);

    // Set disc type based on track 1 mode (MacOS uses this)
    CUETrackInfo trackInfo = CDUtils::GetTrackInfoForTrack(gadget, 1);
    if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO)
    {
        gadget->m_DiscInfoReply.disc_type = 0x00; // CD-DA (audio)
    }
    else
    {
        gadget->m_DiscInfoReply.disc_type = 0x10; // CD-ROM (data)
    }

    u32 leadoutLBA = CDUtils::GetLeadoutLBA(gadget);
    gadget->m_DiscInfoReply.last_lead_in_start_time = htonl(leadoutLBA);
    gadget->m_DiscInfoReply.last_possible_lead_out = htonl(leadoutLBA);

    // Set response length
    u16 allocationLength = gadget->m_CBW.CBWCB[7] << 8 | (gadget->m_CBW.CBWCB[8]);
    int length = sizeof(TUSBDiscInfoReply);
    if (allocationLength < length)
        length = allocationLength;

    memcpy(gadget->m_InBuffer, &gadget->m_DiscInfoReply, length);
    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
}

void SCSITOC::ReadTrackInformation(CUSBCDGadget *gadget)
{
    u8 addressType = gadget->m_CBW.CBWCB[1] & 0x03;
    u32 address = (gadget->m_CBW.CBWCB[2] << 24) | (gadget->m_CBW.CBWCB[3] << 16) |
                  (gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];
    u16 allocationLength = (gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8];

    CDROM_DEBUG_LOG("SCSITOC::ReadTrackInformation",
                    "Read Track Information type=%d, addr=%u", addressType, address);

    if (!gadget->m_CDReady)
    {
        gadget->setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
        return;
    }

    DoReadTrackInformation(gadget, addressType, address, allocationLength);
}

void SCSITOC::DoReadTrackInformation(CUSBCDGadget *gadget, u8 addressType, u32 address, u16 allocationLength)
{
    TUSBCDTrackInformationBlock response;
    memset(&response, 0, sizeof(response));

    CUETrackInfo trackInfo = {0};
    trackInfo.track_number = -1;

    // Find the track based on address type
    if (addressType == 0x00)
    {
        // LBA address
        trackInfo = CDUtils::GetTrackInfoForLBA(gadget, address);
    }
    else if (addressType == 0x01)
    {
        // Logical track number
        trackInfo = CDUtils::GetTrackInfoForTrack(gadget, address);
    }
    else if (addressType == 0x02)
    {
        // Session number - we only support session 1
        if (address == 1)
        {
            gadget->cueParser.restart();
            const CUETrackInfo *first = gadget->cueParser.next_track();
            if (first)
                trackInfo = *first;
        }
    }

    if (trackInfo.track_number == -1)
    {
        gadget->setSenseData(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        gadget->sendCheckCondition();
        return;
    }

    // Calculate track length
    u32 trackLength = 0;
    gadget->cueParser.restart();
    const CUETrackInfo *nextTrack = nullptr;
    const CUETrackInfo *currentTrack = nullptr;

    while ((currentTrack = gadget->cueParser.next_track()) != nullptr)
    {
        if (currentTrack->track_number == trackInfo.track_number)
        {
            nextTrack = gadget->cueParser.next_track();
            if (nextTrack)
            {
                trackLength = nextTrack->data_start - currentTrack->data_start;
            }
            else
            {
                // Last track - calculate from file size
                u32 leadoutLBA = CDUtils::GetLeadoutLBA(gadget);
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

    memcpy(gadget->m_InBuffer, &response, length);
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_nnumber_blocks = 0;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSITOC::ReadHeader(CUSBCDGadget *gadget)
{
    bool MSF = (gadget->m_CBW.CBWCB[1] & 0x02);
    uint32_t lba = (gadget->m_CBW.CBWCB[2] << 24) | (gadget->m_CBW.CBWCB[3] << 16) |
                   (gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];
    uint16_t allocationLength = (gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8];

    CDROM_DEBUG_LOG("SCSITOC::ReadHeader",
                    "Read Header lba=%u, MSF=%d", lba, MSF);

    if (!gadget->m_CDReady)
    {
        gadget->setSenseData(0x02, 0x04, 0x00); // LOGICAL UNIT NOT READY
        gadget->sendCheckCondition();
        return;
    }

    DoReadHeader(gadget, MSF, lba, allocationLength);
}

void SCSITOC::DoReadHeader(CUSBCDGadget *gadget, bool MSF, uint32_t lba, uint16_t allocationLength)
{
    // Terminate audio playback if active (MMC Annex C requirement)
    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer)
    {
        cdplayer->Pause();
    }

    uint8_t mode = 1; // Default to Mode 1

    gadget->cueParser.restart();
    CUETrackInfo trackinfo = CDUtils::GetTrackInfoForLBA(gadget, lba);

    if (trackinfo.track_number != -1 && trackinfo.track_mode == CUETrack_AUDIO)
    {
        mode = 0; // Audio track
    }

    gadget->m_InBuffer[0] = mode;
    gadget->m_InBuffer[1] = 0; // Reserved
    gadget->m_InBuffer[2] = 0; // Reserved
    gadget->m_InBuffer[3] = 0; // Reserved

    // Track start address
    if (MSF)
    {
        gadget->m_InBuffer[4] = 0;
        CDUtils::LBA2MSF(lba, &gadget->m_InBuffer[5], false);
    }
    else
    {
        gadget->m_InBuffer[4] = (lba >> 24) & 0xFF;
        gadget->m_InBuffer[5] = (lba >> 16) & 0xFF;
        gadget->m_InBuffer[6] = (lba >> 8) & 0xFF;
        gadget->m_InBuffer[7] = (lba >> 0) & 0xFF;
    }

    uint8_t len = 8;
    if (len > allocationLength)
        len = allocationLength;

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, len);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_nnumber_blocks = 0;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}

void SCSITOC::ReadSubChannel(CUSBCDGadget *gadget)
{
    unsigned int msf = (gadget->m_CBW.CBWCB[1] >> 1) & 0x01;
    // unsigned int subq = (gadget->m_CBW.CBWCB[2] >> 6) & 0x01; //TODO We're ignoring subq for now
    unsigned int parameter_list = gadget->m_CBW.CBWCB[3];
    // unsigned int track_number = gadget->m_CBW.CBWCB[6]; // Ignore track number for now. It's used only for ISRC
    int allocationLength = (gadget->m_CBW.CBWCB[7] << 8) | gadget->m_CBW.CBWCB[8];
    int length = 0;

    // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42), allocationLength = %d, msf = %u, parameter_list = 0x%02x", allocationLength, msf, parameter_list);

    CCDPlayer *cdplayer = static_cast<CCDPlayer *>(CScheduler::Get()->GetTask("cdplayer"));

    if (parameter_list == 0x00)
        parameter_list = 0x01; // 0x00 is "reserved" so let's assume they want cd info

    switch (parameter_list)
    {
    // Current Position Data request
    case 0x01:
    {
        // Current Position Header
        TUSBCDSubChannelHeaderReply header;
        memset(&header, 0, SIZE_SUBCHANNEL_HEADER_REPLY);
        header.audioStatus = 0x15; // Audio status not supported
        header.dataLength = SIZE_SUBCHANNEL_01_DATA_REPLY;

        // Override audio status by querying the player
        if (cdplayer)
        {
            unsigned int state = cdplayer->GetState();
            switch (state)
            {
            case CCDPlayer::PLAYING:
                header.audioStatus = 0x11; // Playing
                break;
            case CCDPlayer::PAUSED:
                header.audioStatus = 0x12; // Paused
                break;
            case CCDPlayer::STOPPED_OK:
                header.audioStatus = 0x13; // Stopped without error
                break;
            case CCDPlayer::STOPPED_ERROR:
                header.audioStatus = 0x14; // Stopped with error
                break;
            default:
                header.audioStatus = 0x15; // No status to return
                break;
            }
        }

        // Current Position Data
        TUSBCDSubChannel01CurrentPositionReply data;
        memset(&data, 0, SIZE_SUBCHANNEL_01_DATA_REPLY);
        data.dataFormatCode = 0x01;

        u32 address = 0;
        if (cdplayer)
        {
            address = cdplayer->GetCurrentAddress();
            data.absoluteAddress = CDUtils::GetAddress(address, msf, false);
            CUETrackInfo trackInfo = CDUtils::GetTrackInfoForLBA(gadget, address);
            if (trackInfo.track_number != -1)
            {
                data.trackNumber = trackInfo.track_number;
                data.indexNumber = 0x01; // Assume no pregap. Perhaps we need to handle pregap?
                data.relativeAddress = CDUtils::GetAddress(address - trackInfo.track_start, msf, true);
                // Set ADR/Control: ADR=1 (position), Control=0 for audio, 4 for data
                u8 control = (trackInfo.track_mode == CUETrack_AUDIO) ? 0x00 : 0x04;
                data.adrControl = (0x01 << 4) | control;
            }
        }

        // CDROM_DEBUG_LOG("CUSBCDGadget::HandleSCSICommand", "READ SUB-CHANNEL CMD (0x42, 0x01) audio_status %02x, trackNumber %d, address %d, absoluteAddress %08x, relativeAddress %08x", header.audioStatus, data.trackNumber, address, data.absoluteAddress, data.relativeAddress);

        // Determine data lengths
        length = SIZE_SUBCHANNEL_HEADER_REPLY + SIZE_SUBCHANNEL_01_DATA_REPLY;

        // Copy the header & Code Page
        memcpy(gadget->m_InBuffer, &header, SIZE_SUBCHANNEL_HEADER_REPLY);
        memcpy(gadget->m_InBuffer + SIZE_SUBCHANNEL_HEADER_REPLY, &data, SIZE_SUBCHANNEL_01_DATA_REPLY);
        break;
    }

    case 0x02:
    {
        // Media Catalog Number (UPC Bar Code)
        break;
    }

    case 0x03:
    {
        // International Standard Recording Code (ISRC)
        // TODO We're ignoring track number because that's only valid here
        break;
    }

    default:
    {
        // TODO Error
    }
    }

    if (allocationLength < length)
        length = allocationLength;

    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn,
                                                     gadget->m_InBuffer, length);

    gadget->m_nnumber_blocks = 0; // nothing more after this send
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->bmCSWStatus;
}

void SCSITOC::ReadDiscStructure(CUSBCDGadget *gadget)
{
    u8 mediaType = gadget->m_CBW.CBWCB[1] & 0x0f; // Media type (0=DVD, 1=BD)
    u32 address = ((u32)gadget->m_CBW.CBWCB[2] << 24) | ((u32)gadget->m_CBW.CBWCB[3] << 16) |
                  ((u32)gadget->m_CBW.CBWCB[4] << 8) | gadget->m_CBW.CBWCB[5];
    u8 layer = gadget->m_CBW.CBWCB[6];
    u8 format = gadget->m_CBW.CBWCB[7];
    u16 allocationLength = ((u16)gadget->m_CBW.CBWCB[8] << 8) | gadget->m_CBW.CBWCB[9];
    u8 agid = (gadget->m_CBW.CBWCB[10] >> 6) & 0x03; // Authentication Grant ID

    CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                    "READ DISC STRUCTURE: media=%d, format=0x%02x, layer=%d, address=0x%08x, alloc=%d, AGID=%d, mediaType=%d",
                    mediaType, format, layer, address, allocationLength, agid, (int)gadget->m_mediaType);

    // For CD media and DVD-specific formats: return minimal empty response
    // MacOS doesn't handle CHECK CONDITION well for this command - causes USB reset
    if (gadget->m_mediaType != MEDIA_TYPE::DVD &&
        (format == 0x00 || format == 0x02 || format == 0x03 || format == 0x04))
    {
        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE format 0x%02x for CD media - returning minimal response", format);

        // Return minimal header indicating no data available
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(header));
        header.dataLength = htons(2); // Just header, no payload

        int length = sizeof(header);
        if (allocationLength < length)
            length = allocationLength;

        memcpy(gadget->m_InBuffer, &header, length);
        gadget->m_nnumber_blocks = 0;
        gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, length);
        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        return;
    }

    // Process DVD structures
    int dataLength = 0;

    switch (format)
    {
    case 0x00: // Physical Format Information
    {
        if (gadget->m_mediaType != MEDIA_TYPE::DVD)
        {
            // Not supported for CD
            CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                            "READ DISC STRUCTURE format 0x00 not supported for CD media");
            gadget->setSenseData(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
            gadget->sendCheckCondition();
            return;
        }

        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE format 0x00: Physical Format Information");

        // Build response: Header + Physical Format Info
        TUSBCDReadDiscStructureHeader header;
        DVDPhysicalFormatInfo physInfo;

        memset(&header, 0, sizeof(header));
        memset(&physInfo, 0, sizeof(physInfo));

        // Calculate disc capacity (you may want to get this from your CUE parser)
        u32 discCapacity = 2298496; // Default: ~4.7GB single-layer DVD-ROM (2,298,496 sectors)

        // Byte 0: Book type and part version
        // Book type: 0x00 = DVD-ROM, 0x0A = DVD+R
        // Part version: 0x01 for DVD-ROM
        physInfo.bookTypePartVer = 0x01; // DVD-ROM, version 1.0

        // Byte 1: Disc size and maximum rate
        // Disc size: 0x00 = 120mm, 0x01 = 80mm
        // Max rate: 0x02 = 10.08 Mbps
        physInfo.discSizeMaxRate = 0x20; // Max rate=2, disc size=0

        // Byte 2: Layers, path, type
        // Num layers: 0x00 = 1 layer, 0x01 = 2 layers
        // Track path: 0 = Parallel
        // Layer type: 0x01 = embossed data layer (bit 0)
        physInfo.layersPathType = 0x01; // Single layer, parallel, embossed

        // Byte 3: Densities
        // Linear density: 0x00 = 0.267 um/bit
        // Track density: 0x00 = 0.74 um/track
        physInfo.densities = 0x00;

        // Bytes 4-6: Data start sector (24-bit big-endian)
        // Standard DVD starts at 0x030000
        u32 dataStart = 0x030000;
        physInfo.dataStartSector[0] = (dataStart >> 16) & 0xFF;
        physInfo.dataStartSector[1] = (dataStart >> 8) & 0xFF;
        physInfo.dataStartSector[2] = dataStart & 0xFF;

        // Bytes 7-9: Data end sector (24-bit big-endian)
        u32 dataEnd = dataStart + discCapacity;
        physInfo.dataEndSector[0] = (dataEnd >> 16) & 0xFF;
        physInfo.dataEndSector[1] = (dataEnd >> 8) & 0xFF;
        physInfo.dataEndSector[2] = dataEnd & 0xFF;

        // Bytes 10-12: Layer 0 end (24-bit big-endian)
        // For single layer, this is 0
        physInfo.layer0EndSector[0] = 0x00;
        physInfo.layer0EndSector[1] = 0x00;
        physInfo.layer0EndSector[2] = 0x00;

        // Byte 13: BCA flag
        physInfo.bcaFlag = 0x00; // No BCA

        // Bytes 14-16: Reserved
        physInfo.reserved[0] = 0x00;
        physInfo.reserved[1] = 0x00;
        physInfo.reserved[2] = 0x00;

        // Set header length (excludes the header itself)
        header.dataLength = htons(sizeof(physInfo));

        // Copy to buffer
        memcpy(gadget->m_InBuffer, &header, sizeof(header));
        dataLength += sizeof(header);
        memcpy(gadget->m_InBuffer + dataLength, &physInfo, sizeof(physInfo));
        dataLength += sizeof(physInfo);

        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "DVD Physical Format: dataStart=0x%06x, dataEnd=0x%06x, totalLength=%d",
                        dataStart, dataEnd, dataLength);
        break;
    }

    case 0x01: // Copyright Information
    {
        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE format 0x01: Copyright Information (CSS=%d)",
                        gadget->m_bReportDVDCSS);

        // Build response: Header + Copyright Info
        TUSBCDReadDiscStructureHeader header;
        DVDCopyrightInfo copyInfo;

        memset(&header, 0, sizeof(header));
        memset(&copyInfo, 0, sizeof(copyInfo));

        // Set copyright protection type
        if (gadget->m_bReportDVDCSS && gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            copyInfo.copyrightProtectionType = 0x01; // CSS/CPPM
            copyInfo.regionManagementInfo = 0x00;    // All regions (0xFF = no regions)
        }
        else
        {
            copyInfo.copyrightProtectionType = 0x00; // No protection
            copyInfo.regionManagementInfo = 0x00;    // N/A
        }

        copyInfo.reserved1 = 0x00;
        copyInfo.reserved2 = 0x00;

        // Set header length
        header.dataLength = htons(sizeof(copyInfo));

        // Copy to buffer
        memcpy(gadget->m_InBuffer, &header, sizeof(header));
        dataLength += sizeof(header);
        memcpy(gadget->m_InBuffer + dataLength, &copyInfo, sizeof(copyInfo));
        dataLength += sizeof(copyInfo);
        break;
    }

    case 0x04: // Manufacturing Information
    {
        if (gadget->m_mediaType != MEDIA_TYPE::DVD)
        {
            CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                            "READ DISC STRUCTURE format 0x04 not supported for CD media");
            gadget->setSenseData(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
            gadget->sendCheckCondition();
            return;
        }

        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE format 0x04: Manufacturing Information");

        // Return 2048 bytes of zeroed manufacturing data
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(header));
        header.dataLength = htons(2048);

        memcpy(gadget->m_InBuffer, &header, sizeof(header));
        dataLength += sizeof(header);

        // Add 2048 bytes of zeros
        memset(gadget->m_InBuffer + dataLength, 0, 2048);
        dataLength += 2048;
        break;
    }

    case 0xFF: // Format List
    {
        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE format 0xFF: Disc Structure List");

        // Build list of supported formats
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(header));

        if (gadget->m_mediaType == MEDIA_TYPE::DVD)
        {
            // DVD supports: 0x00 (Physical), 0x01 (Copyright), 0x04 (Manufacturing), 0xFF (List)
            u8 formatList[] = {
                0x00, 0x00, 0x00, 0x00, // Format 0x00: Physical Format
                0x01, 0x00, 0x00, 0x00, // Format 0x01: Copyright
                0x04, 0x00, 0x00, 0x00, // Format 0x04: Manufacturing
                0xFF, 0x00, 0x00, 0x00  // Format 0xFF: List
            };

            header.dataLength = htons(sizeof(formatList));
            memcpy(gadget->m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);
            memcpy(gadget->m_InBuffer + dataLength, formatList, sizeof(formatList));
            dataLength += sizeof(formatList);
        }
        else
        {
            // CD only supports: 0x01 (Copyright), 0xFF (List)
            u8 formatList[] = {
                0x01, 0x00, 0x00, 0x00, // Format 0x01: Copyright
                0xFF, 0x00, 0x00, 0x00  // Format 0xFF: List
            };

            header.dataLength = htons(sizeof(formatList));
            memcpy(gadget->m_InBuffer, &header, sizeof(header));
            dataLength += sizeof(header);
            memcpy(gadget->m_InBuffer + dataLength, formatList, sizeof(formatList));
            dataLength += sizeof(formatList);
        }
        break;
    }

    default: // Unsupported format
    {
        CDROM_DEBUG_LOG("SCSITOC::ReadDiscStructure",
                        "READ DISC STRUCTURE: Unsupported format 0x%02x", format);

        // Return minimal empty structure
        TUSBCDReadDiscStructureHeader header;
        memset(&header, 0, sizeof(header));
        header.dataLength = htons(0); // No data

        memcpy(gadget->m_InBuffer, &header, sizeof(header));
        dataLength += sizeof(header);
        break;
    }
    }

    // Truncate to allocation length
    if (allocationLength < dataLength)
        dataLength = allocationLength;

    // Send response
    gadget->m_nnumber_blocks = 0;
    gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->m_InBuffer, dataLength);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
}
