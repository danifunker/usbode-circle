#include "isdprotocol.h"
#include <circle/logger.h>
#include <circle/util.h>

static const char LogName[] = "isdproto";

ISDProtocol::ISDProtocol(IImageDevice *pDevice)
    : m_nLastStatus(0x5B),
      m_pDevice(pDevice),
      m_nResponseDataLength(0),
      m_nResponseDataOffset(0)
{
    memset(m_ResponseBuffer, 0, sizeof(m_ResponseBuffer));
    CLogger::Get()->Write(LogName, LogNotice, "ISD Protocol handler initialized");
}

ISDProtocol::~ISDProtocol()
{
}

void ISDProtocol::SetDevice(IImageDevice *pDevice)
{
    m_pDevice = pDevice;
    CLogger::Get()->Write(LogName, LogNotice, "ISD Protocol device updated");
}

void ISDProtocol::SetStatus(u8 nStatus)
{
    if (m_nLastStatus != nStatus)
    {
        CString logMsg;
        logMsg.Format("ISD: Status changed: 0x%02X -> 0x%02X", m_nLastStatus, nStatus);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        m_nLastStatus = nStatus;
    }
}

void ISDProtocol::NotifyTransferComplete()
{
    SetStatus(0x5B);  // Ready
    CLogger::Get()->Write(LogName, LogNotice, "ISD: Bulk transfer completed, status=0x5B (ready)");
}

bool ISDProtocol::HandleControlTransfer(u8 bmRequestType, u8 bRequest,
                                        u16 wValue, u16 wIndex, u16 wLength,
                                        u8 *pData, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD Control Transfer: bmRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%d",
                  bmRequestType, bRequest, wValue, wIndex, wLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    logMsg.Format("ISD: Current device status = 0x%02X", m_nLastStatus);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);

    // bRequest=5 is STATUS REQUEST (device-to-host)
    if (bmRequestType == 0xC0 && bRequest == 0x05)
    {
        return HandleStatusRequest(pData, pResponseLength);
    }
    
    // Handle different request types
    if (bmRequestType == 0x23 && bRequest == 0x02)
    {
        // Class request to interface - likely CLEAR_FEATURE
        *pResponseLength = 0;
        SetStatus(0x5B);  // Command completed successfully
        return true;
    }
    
    if (bmRequestType == 0x40 && bRequest == 0x04)
    {
        // Vendor command 0x04 - unknown purpose, just ACK
        logMsg.Format("ISD: Vendor command 04 (wValue=%04x) - ACK", wValue);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        *pResponseLength = 0;
        SetStatus(0x5B);  // Command completed
        return true;
    }

if (bmRequestType == 0x40 && bRequest == 0x06)
{
    // Vendor command 0x06 - This is the main ISD command batch!
    size_t responseLen = 0;
    bool result = HandleCommandBatch(pData, wLength, &responseLen);
    
    // CRITICAL: Control OUT must ACK with zero length
    // The actual response (136 bytes) is already buffered in m_ResponseBuffer
    // and will be sent later via bulk IN endpoint 0x82
    *pResponseLength = 0;  // ACK the control transfer with no data
    
    SetStatus(result ? 0x48 : 0x48);  // Set busy while processing
    
    return result;
}

    CLogger::Get()->Write(LogName, LogWarning, "ISD: Unhandled control transfer");
    SetStatus(0x48);  // Error status
    return false;
}

bool ISDProtocol::HandleCommandBatch(u8 *pData, size_t nLength, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Parsing command batch (%u bytes)", (unsigned)nLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);

    // Dump the batch for debugging
    DumpCommandBatch(pData, nLength);

    // Reset response buffer
    m_nResponseDataLength = 0;
    m_nResponseDataOffset = 0;

    // Parse commands and accumulate responses in m_ResponseBuffer
    size_t responseLength = 0;
    bool result = ParseCommandBatch(pData, nLength, &responseLength);
    
    if (result && responseLength > 0)
    {
        logMsg.Format("ISD: Command batch generated %u response bytes (stored for bulk IN)", 
                     (unsigned)responseLength);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        m_nResponseDataLength = responseLength;
        m_nResponseDataOffset = 0;
        SetStatus(0x48);  // Set to BUSY - will be cleared after bulk transfer
    }
    else
    {
        SetStatus(0x5B);  // Still success even if no response data
    }

    // For control transfer, return 0 (no data on control endpoint)
    *pResponseLength = 0;
    return true;
}

bool ISDProtocol::HandleStatusRequest(u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Status request - returning 0x%02X", m_nLastStatus);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Return 6-byte status response
    pResponse[0] = m_nLastStatus;  // Status byte (0x5B = ready, 0x48 = busy)
    pResponse[1] = 0x00;
    pResponse[2] = 0x00;
    pResponse[3] = 0x40;
    pResponse[4] = 0x00;
    pResponse[5] = 0x00;
    
    *pResponseLength = 6;
    return true;
}

void ISDProtocol::DumpCommandBatch(const u8 *pData, size_t nLength)
{
    CString logMsg;
    logMsg.Format("ISD Batch (%u bytes):", (unsigned)nLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);

    // Dump in hex, 16 bytes per line
    for (size_t i = 0; i < nLength; i += 16)
    {
        CString line;
        line.Format("  %04x:", (unsigned)i);

        for (size_t j = 0; j < 16 && (i + j) < nLength; j++)
        {
            CString byte;
            byte.Format(" %02x", pData[i + j]);
            line.Append(byte);
        }

        CLogger::Get()->Write(LogName, LogNotice, line);
    }
}

bool ISDProtocol::ParseCommandBatch(u8 *pData, size_t nLength, size_t *pResponseLength)
{
    // Special case: First command batch is initialization (no 0xAA delimiters)
    if (nLength == 17 && pData[0] != 0xAA)
    {
        CString logMsg;
        logMsg.Format("ISD: Init sequence detected (17 bytes, no AA delimiters)");
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        // Dump the init sequence for analysis
        logMsg.Format("ISD Init: %02x %02x %02x %02x %02x %02x %02x %02x...",
                     pData[0], pData[1], pData[2], pData[3],
                     pData[4], pData[5], pData[6], pData[7]);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        // This appears to be parameter setting/initialization, not command execution
        // Just ACK and return ready status (no response data needed)
        SetStatus(0x5B);  // Ready
        *pResponseLength = 0;
        return true;
    }

    size_t offset = 0;
    size_t responseOffset = 0;
    size_t commandCount = 0;
    
    // Track individual command response sizes for debugging
    struct CommandInfo {
        u8 code;
        size_t responseSize;
    };
    CommandInfo commands[10];  // Max 10 commands per batch
    
    while (offset < nLength)
    {
        // Look for 0xAA delimiter
        if (pData[offset] == 0xAA && (offset + 1) < nLength)
        {
            u8 commandCode = pData[offset + 1];
            
            CString logMsg;
            logMsg.Format("ISD: Found command AA %02X at offset %u", commandCode, (unsigned)offset);
            CLogger::Get()->Write(LogName, LogNotice, logMsg);
            
            // Find the length of this command
            size_t cmdStart = offset;
            size_t cmdEnd = offset + 2;
            
            while (cmdEnd < nLength && pData[cmdEnd] != 0xAA)
            {
                cmdEnd++;
            }
            
            size_t cmdLength = cmdEnd - cmdStart;
            
            // Process command - write response directly to m_ResponseBuffer
            size_t cmdResponseLen = 0;
            if (responseOffset < MaxResponseSize)
            {
                if (ProcessCommand(commandCode, &pData[cmdStart], cmdLength, 
                                 &m_ResponseBuffer[responseOffset], &cmdResponseLen))
                {
                    if (commandCount < 10)
                    {
                        commands[commandCount].code = commandCode;
                        commands[commandCount].responseSize = cmdResponseLen;
                        commandCount++;
                    }
                    responseOffset += cmdResponseLen;
                }
            }
            
            offset = cmdEnd;
        }
        else
        {
            offset++;
        }
    }
    
    *pResponseLength = responseOffset;
    
    if (responseOffset == 0)
    {
        CLogger::Get()->Write(LogName, LogNotice, "ISD: Command batch ACKed (no response data)");
    }
    else
    {
        // Log detailed breakdown
        CString logMsg;
        logMsg.Format("ISD: Command batch complete - Response breakdown:");
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        for (size_t i = 0; i < commandCount; i++)
        {
            logMsg.Format("  CMD 0x%02X: %u bytes", 
                         commands[i].code, (unsigned)commands[i].responseSize);
            CLogger::Get()->Write(LogName, LogNotice, logMsg);
        }
        
        logMsg.Format("  TOTAL: %u bytes (expected ~136 for Mac compatibility)", 
                     (unsigned)responseOffset);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    return true;
}

bool ISDProtocol::ProcessCommand(u8 commandCode, const u8 *pCmdData, size_t nCmdLength,
                                 u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Processing command %02X (%u bytes)", commandCode, (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);

    switch (commandCode)
    {
    case 0x0E:  // MODE SENSE page 0x0E (Audio Control)
        return HandleModeSense0E(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    case 0x14:  // Unknown vendor command
        return HandleCommand14(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    case 0x15:  // Unknown vendor command
        return HandleCommand15(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    case 0x16:  // Possibly READ TOC or capabilities
    {
        // Calculate maxLBA from device if available
        u32 maxLBA = 0;
        if (m_pDevice)
        {
            u64 deviceSize = m_pDevice->GetSize();
            maxLBA = (u32)(deviceSize / 2048);  // Convert bytes to 2048-byte sectors
            
            logMsg.Format("ISD: Device size = %llu bytes, maxLBA = %u sectors", 
                         deviceSize, maxLBA);
            CLogger::Get()->Write(LogName, LogNotice, logMsg);
        }
        return HandleCommand16(pCmdData, nCmdLength, pResponse, pResponseLength, maxLBA);
    }        
    case 0x17:  // Unknown vendor command
        return HandleCommand17(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    default:
        logMsg.Format("ISD: Unknown command %02X", commandCode);
        CLogger::Get()->Write(LogName, LogWarning, logMsg);
        *pResponseLength = 0;
        return false;
    }
}

bool ISDProtocol::HandleModeSense0E(const u8 *pCmdData, size_t nCmdLength,
                                    u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: MODE SENSE 0x0E (Audio Control)");
    
    // Return minimal audio control page
    // Format: page header + audio control parameters
    memset(pResponse, 0, 20);
    pResponse[0] = 0x0E;  // Page code
    pResponse[1] = 0x0E;  // Page length (14 bytes)
    pResponse[2] = 0x05;  // IMMED + SOTC flags
    pResponse[3] = 0x01;  // Output port 0 select (channel 0)
    pResponse[4] = 0xFF;  // Output port 0 volume (max)
    pResponse[5] = 0x02;  // Output port 1 select (channel 1)
    pResponse[6] = 0xFF;  // Output port 1 volume (max)
    
    *pResponseLength = 20;
    return true;
}

bool ISDProtocol::HandleCommand14(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x14 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x14 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Command 0x14 - Device Status/Capabilities Query
    // This appears to query device capabilities and current state
    memset(pResponse, 0, 32);
    
    // Header (4 bytes)
    pResponse[0] = 0x00;  // Status: OK
    pResponse[1] = 0x1E;  // Data length following (30 bytes)
    pResponse[2] = 0x00;  // Reserved
    pResponse[3] = 0x00;  // Reserved
    
    // Device Type/Capabilities (8 bytes)
    pResponse[4] = 0x05;  // Device type: CD-ROM
    pResponse[5] = 0x00;  // Removable media flag
    pResponse[6] = 0x00;  // Reserved
    pResponse[7] = 0x00;  // Reserved
    pResponse[8] = 0x02;  // Supports: (bit 1 = audio)
    pResponse[9] = 0x00;  // Write capabilities: none
    pResponse[10] = 0x01; // Read capabilities: audio
    pResponse[11] = 0x00; // Reserved
    
    // Media Status (4 bytes)
    pResponse[12] = 0x01; // Media present
    pResponse[13] = 0x00; // Door closed
    pResponse[14] = 0x00; // Not spinning up
    pResponse[15] = 0x00; // Reserved
    
    // Audio Features (8 bytes)
    pResponse[16] = 0x02; // Number of audio channels
    pResponse[17] = 0xFF; // Max volume (255)
    pResponse[18] = 0xFF; // Current volume L
    pResponse[19] = 0xFF; // Current volume R
    pResponse[20] = 0x00; // Mute status: off
    pResponse[21] = 0x00; // Reserved
    pResponse[22] = 0x00; // Reserved
    pResponse[23] = 0x00; // Reserved
    
    // Additional info (8 bytes) - leave as zeros
    
    *pResponseLength = 32;
    
    logMsg.Format("ISD: Command 0x14 returning %u bytes (device status)", 
                 (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand15(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x15 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x15 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Command 0x15 - Playback Status/Position Query
    // This might query current playback state and position
    memset(pResponse, 0, 32);
    
    // Header (4 bytes)
    pResponse[0] = 0x00;  // Status: OK
    pResponse[1] = 0x1C;  // Data length (28 bytes)
    pResponse[2] = 0x00;  // Reserved
    pResponse[3] = 0x00;  // Reserved
    
    // Playback State (8 bytes)
    pResponse[4] = 0x00;  // Audio status: 00 = stopped
                          // 0x11 = playing, 0x12 = paused, 0x13 = completed
    pResponse[5] = 0x01;  // Current track (track 1)
    pResponse[6] = 0x00;  // Current index
    pResponse[7] = 0x00;  // Reserved
    
    // Current Position MSF (4 bytes)
    pResponse[8] = 0x00;  // Reserved
    pResponse[9] = 0x00;  // Minutes
    pResponse[10] = 0x00; // Seconds
    pResponse[11] = 0x00; // Frames
    
    // Track Start MSF (4 bytes)
    pResponse[12] = 0x00; // Reserved
    pResponse[13] = 0x02; // Start: 00:02:00 (track 1)
    pResponse[14] = 0x00;
    pResponse[15] = 0x00;
    
    // Track End MSF (4 bytes)
    pResponse[16] = 0x00; // Reserved
    pResponse[17] = 0x00; // Minutes (calculated from track length)
    pResponse[18] = 0x27; // Seconds
    pResponse[19] = 0x62; // Frames
    
    // Disc Mode/Type (8 bytes)
    pResponse[20] = 0x01; // Disc type: Audio CD
    pResponse[21] = 0x02; // Number of tracks
    pResponse[22] = 0x00; // Current program: none
    pResponse[23] = 0x00; // Repeat mode: off
    pResponse[24] = 0x00; // Random mode: off
    pResponse[25] = 0x00; // Reserved
    pResponse[26] = 0x00; // Reserved
    pResponse[27] = 0x00; // Reserved
    
    // Reserved (4 bytes)
    
    *pResponseLength = 32;
    
    logMsg.Format("ISD: Command 0x15 returning %u bytes (playback status)", 
                 (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand16(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength,
                                  u32 maxLBA)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x16 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x16 params: param1=0x%02X param2=0x%02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Get track info from device CUE sheet
    int numTracks = 1;
    u32 track2Start = 0;
    bool hasAudioTrack = false;
    
    if (m_pDevice)
    {
        numTracks = m_pDevice->GetNumTracks();
        
        logMsg.Format("ISD: Device reports %d tracks", numTracks);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        // Try to get track start positions
        if (numTracks >= 2)
        {
            // Try track index 1 (0-based) for track 2
            track2Start = m_pDevice->GetTrackStart(1);
            
            // If that's 0, try index 2 (1-based)
            if (track2Start == 0)
            {
                track2Start = m_pDevice->GetTrackStart(2);
            }
            
            hasAudioTrack = m_pDevice->IsAudioTrack(2);
            
            logMsg.Format("ISD: Track 2 start LBA = %u (audio=%d)", track2Start, hasAudioTrack);
            CLogger::Get()->Write(LogName, LogNotice, logMsg);
            
            // If still 0, something's wrong - use a sensible default
            if (track2Start == 0 && numTracks > 1)
            {
                // Typical data track is ~300MB = ~150,000 sectors
                // For our test disc, track 1 is 2912 sectors
                // Let's try to calculate from maxLBA
                track2Start = maxLBA / 2;  // Rough estimate if method fails
                
                logMsg.Format("ISD: WARNING - GetTrackStart() returned 0, using estimate: %u", track2Start);
                CLogger::Get()->Write(LogName, LogWarning, logMsg);
            }
        }
    }
    
    memset(pResponse, 0, 52);
    
    // TOC Header
    pResponse[0] = 0x00;
    pResponse[1] = 50;              // TOC data length (52 - 2)
    pResponse[2] = 0x01;           // First track
    pResponse[3] = numTracks;      // Last track
    
    // Track 1 descriptor (data track)
    pResponse[4] = 0x00;
    pResponse[5] = 0x14;           // Data track
    pResponse[6] = 0x01;           // Track 1
    pResponse[7] = 0x00;
    pResponse[8] = 0x00;           // 00:02:00
    pResponse[9] = 0x02;
    pResponse[10] = 0x00;
    pResponse[11] = 0x00;
    
    size_t offset = 12;
    
    // Track 2 descriptor (if multi-track)
    if (numTracks >= 2)
    {
        u32 track2LBA = track2Start + 150;  // Add pregap
        u8 t2_min = track2LBA / (60 * 75);
        u8 t2_sec = (track2LBA / 75) % 60;
        u8 t2_frame = track2LBA % 75;
        
        pResponse[offset++] = 0x00;
        pResponse[offset++] = hasAudioTrack ? 0x10 : 0x14;  // Audio or data
        pResponse[offset++] = 0x02;        // Track 2
        pResponse[offset++] = 0x00;
        pResponse[offset++] = t2_min;
        pResponse[offset++] = t2_sec;
        pResponse[offset++] = t2_frame;
        pResponse[offset++] = 0x00;
        
        logMsg.Format("ISD: Track 2 MSF: %02u:%02u:%02u (LBA %u with pregap)",
                     t2_min, t2_sec, t2_frame, track2LBA);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Lead-out descriptor
    if (maxLBA > 0)
    {
        u32 leadOutSector = maxLBA + 150;
        u8 leadOutMin = leadOutSector / (60 * 75);
        u8 leadOutSec = (leadOutSector / 75) % 60;
        u8 leadOutFrame = leadOutSector % 75;
        
        pResponse[offset++] = 0x00;
        pResponse[offset++] = 0x14;
        pResponse[offset++] = 0xAA;        // Lead-out
        pResponse[offset++] = 0x00;
        pResponse[offset++] = leadOutMin;
        pResponse[offset++] = leadOutSec;
        pResponse[offset++] = leadOutFrame;
        pResponse[offset++] = 0x00;
        
        logMsg.Format("ISD: Lead-out MSF: %02u:%02u:%02u (sector %u)",
                     leadOutMin, leadOutSec, leadOutFrame, leadOutSector);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    *pResponseLength = 52;
    
    logMsg.Format("ISD: Command 0x16 returning %u bytes (%d track TOC)",
                 (unsigned)*pResponseLength, numTracks);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand17(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x17 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x17 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Command 0x17 - Configuration/Feature Query
    // Minimal response for now
    memset(pResponse, 0, 16);
    
    pResponse[0] = 0x00;  // Status: OK
    pResponse[1] = 0x0E;  // Data length (14 bytes)
    pResponse[2] = 0x00;  // Reserved
    pResponse[3] = 0x00;  // Reserved
    
    // Feature flags
    pResponse[4] = 0x01;  // Supports standard features
    pResponse[5] = 0x00;  // No advanced features
    
    *pResponseLength = 16;
    
    logMsg.Format("ISD: Command 0x17 returning %u bytes", (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::GetPendingResponseData(u8 *pBuffer, size_t nMaxLength, size_t *pActualLength)
{
    // Check if we have any data to send
    if (m_nResponseDataOffset > 0)
    {
        // Already sent first packet, don't send more
        *pActualLength = 0;
        m_nResponseDataLength = 0;  // Clear everything
        m_nResponseDataOffset = 0;
        return false;
    }
        
    size_t remaining = m_nResponseDataLength - m_nResponseDataOffset;
    size_t toSend = (remaining < nMaxLength) ? remaining : nMaxLength;
    
    memcpy(pBuffer, &m_ResponseBuffer[m_nResponseDataOffset], toSend);
    
    // HEX DUMP THE ACTUAL DATA being sent
    CString logMsg;
    logMsg.Format("ISD: Bulk IN sending %u bytes (%u/%u total):", 
                  (unsigned)toSend, 
                  (unsigned)(m_nResponseDataOffset + toSend), 
                  (unsigned)m_nResponseDataLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Dump hex + ASCII
    for (size_t i = 0; i < toSend; i += 16)
    {
        CString hexLine;
        CString asciiLine;
        
        for (size_t j = 0; j < 16 && (i + j) < toSend; j++)
        {
            CString byte;
            byte.Format("%02x ", pBuffer[i + j]);
            hexLine.Append(byte);
            
            char c = (pBuffer[i + j] >= 32 && pBuffer[i + j] < 127) 
                     ? pBuffer[i + j] : '.';
            CString charStr;
            charStr.Format("%c", c);
            asciiLine.Append(charStr);
        }
        
        CString line;
        line.Format("  %04x: %-48s %s", 
                    (unsigned)(m_nResponseDataOffset + i), 
                    (const char*)hexLine, 
                    (const char*)asciiLine);
        CLogger::Get()->Write(LogName, LogNotice, line);
    }
    
    m_nResponseDataOffset += toSend;
    *pActualLength = toSend;
    
    // CRITICAL: Only notify completion when ALL data has been sent
    if (m_nResponseDataOffset >= m_nResponseDataLength)
    {
        CLogger::Get()->Write(LogName, LogNotice, "ISD: All bulk IN data sent, notifying completion");
        m_nResponseDataLength = 0;
        m_nResponseDataOffset = 0;
        NotifyTransferComplete();  // NOW set status to 0x5B (ready)
    }
    else
    {
        logMsg.Format("ISD: More data pending (%u bytes remaining)", 
                     (unsigned)(m_nResponseDataLength - m_nResponseDataOffset));
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    return true;
}
bool ISDProtocol::HandleModeSense(const u8 *pCDB, u8 *pDataBuffer,
                                 size_t nBufferSize, size_t *pResponseLength)
{
    // This is for standard SCSI MODE SENSE over bulk endpoints
    // Not used in ISD mode - just return false
    *pResponseLength = 0;
    return false;
}

bool ISDProtocol::GetAllPendingResponseData(u8 *pBuffer, size_t nMaxLength, size_t *pActualLength)
{
    if (m_nResponseDataLength == 0)
    {
        *pActualLength = 0;
        return false;
    }
    
    if (m_nResponseDataLength > nMaxLength)
    {
        CLogger::Get()->Write(LogName, LogError, "Response data too large for buffer");
        *pActualLength = 0;
        return false;
    }
    
    // Copy ALL response data at once
    memcpy(pBuffer, m_ResponseBuffer, m_nResponseDataLength);
    *pActualLength = m_nResponseDataLength;
    
    CString logMsg;
    logMsg.Format("ISD: Sending ALL %u bytes in single transfer", (unsigned)m_nResponseDataLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Clear the response buffer
    m_nResponseDataLength = 0;
    m_nResponseDataOffset = 0;
    
    return true;
}