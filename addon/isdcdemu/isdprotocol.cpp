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
        bool result = HandleCommandBatch(pData, wLength, pResponseLength);
        SetStatus(result ? 0x5B : 0x48);  // Success or error
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
    
    // Parse command parameters if present
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];  // First param after AA 14
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x14 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Return 32-byte response
    memset(pResponse, 0, 32);
    
    // Stub response - device status/capabilities?
    pResponse[0] = 0x00;  // Status: OK
    pResponse[1] = 0x00;
    pResponse[2] = 0x00;
    pResponse[3] = 0x01;  // Some capability flag
    
    *pResponseLength = 32;
    
    logMsg.Format("ISD: Command 0x14 returning %u bytes", (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand15(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x15 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters if present
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];  // First param after AA 15
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x15 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Return 32-byte response
    memset(pResponse, 0, 32);
    
    // Stub response
    pResponse[0] = 0x00;  // Status: OK
    pResponse[1] = 0x00;
    pResponse[2] = 0x00;
    pResponse[3] = 0xFF;  // Some value
    
    *pResponseLength = 32;
    
    logMsg.Format("ISD: Command 0x15 returning %u bytes", (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand16(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength, u32 maxLBA)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x16 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];  // 0x80
        u8 param2 = pCmdData[3];  // 0x05
        
        logMsg.Format("ISD: CMD 0x16 params: param1=0x%02X param2=0x%02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        // param2 might be a format specifier:
        // 0x05 might mean "full TOC with MSF addressing"
    }
    
    // Try to build TOC from actual device if available
    if (m_pDevice)
    {
        u64 nTotalBytes = m_pDevice->GetSize();
        u32 nTotalSectors = (u32)(nTotalBytes / 2048);  // Assume 2048-byte sectors
        
        logMsg.Format("ISD: CMD 0x16 - Device has %u sectors", nTotalSectors);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        // Build proper TOC response
        memset(pResponse, 0, 52);
        
        pResponse[0] = 0x00;           // Reserved
        pResponse[1] = 46;              // TOC data length (48 - 2)
        pResponse[2] = 0x01;           // First track
        pResponse[3] = 0x01;           // Last track (single track for now)
        
        // Track 1 descriptor
        pResponse[4] = 0x00;           // Reserved
        pResponse[5] = 0x14;           // ADR=1, Control=4 (data track, digital copy permitted)
        pResponse[6] = 0x01;           // Track number
        pResponse[7] = 0x00;           // Reserved
        
        // Start MSF: 00:02:00 (sector 150 - standard CD pregap)
        pResponse[8] = 0x00;           // Minutes
        pResponse[9] = 0x02;           // Seconds
        pResponse[10] = 0x00;          // Frames
        pResponse[11] = 0x00;          // Reserved
        
        // Lead-out descriptor
        pResponse[12] = 0x00;          // Reserved
        pResponse[13] = 0x14;          // ADR=1, Control=4
        pResponse[14] = 0xAA;          // Track number = lead-out
        pResponse[15] = 0x00;          // Reserved
        
        // Convert total sectors to MSF for lead-out
        u32 leadOutSector = nTotalSectors + 150;  // Add pregap
        u8 leadOutMin = leadOutSector / (60 * 75);
        u8 leadOutSec = (leadOutSector / 75) % 60;
        u8 leadOutFrame = leadOutSector % 75;
        
        pResponse[16] = leadOutMin;
        pResponse[17] = leadOutSec;
        pResponse[18] = leadOutFrame;
        pResponse[19] = 0x00;          // Reserved
        
        logMsg.Format("ISD: CMD 0x16 - Lead-out MSF: %02u:%02u:%02u (sector %u)",
                     leadOutMin, leadOutSec, leadOutFrame, leadOutSector);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        
        *pResponseLength = 52;
        return true;
    }
    
    // Fallback stub if no device
    memset(pResponse, 0, 48);
    
    pResponse[0] = 0x00;   // Reserved
    pResponse[1] = 0x12;   // TOC length MSB
    pResponse[2] = 0x01;   // First track
    pResponse[3] = 0x02;   // Last track (2 tracks)
    
    // Track 1 descriptor (data track)
    pResponse[4] = 0x00;   // Reserved
    pResponse[5] = 0x04;   // ADR/Control
    pResponse[6] = 0x01;   // Track number 1
    pResponse[7] = 0x00;   // Reserved
    pResponse[8] = 0x00;   // Track start address (MSB)
    pResponse[9] = 0x00;
    pResponse[10] = 0x00;
    pResponse[11] = 0x00;  // Track start address (LSB)
    
    // Track 2 descriptor (audio track)
    pResponse[12] = 0x00;  // Reserved
    pResponse[13] = 0x10;  // ADR/Control (audio track)
    pResponse[14] = 0x02;  // Track number 2
    pResponse[15] = 0x00;  // Reserved
    pResponse[16] = 0x00;  // Track start address (MSB)
    pResponse[17] = 0x00;
    pResponse[18] = 0x0B;  // ~2900 sectors
    pResponse[19] = 0x60;  // Track start address (LSB)
    
    // Lead-out track descriptor
    pResponse[20] = 0x00;  // Reserved
    pResponse[21] = 0x10;  // ADR/Control
    pResponse[22] = 0xAA;  // Lead-out track marker
    pResponse[23] = 0x00;  // Reserved
    pResponse[24] = 0x00;  // Lead-out address (MSB)
    pResponse[25] = 0x00;
    pResponse[26] = 0x49;  // Total ~18724 sectors
    pResponse[27] = 0x24;  // Lead-out address (LSB)
    
    *pResponseLength = 52;
    
    logMsg.Format("ISD: Command 0x16 returning %u bytes (TOC stub)", (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::HandleCommand17(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD: Command 0x17 (length=%u bytes)", (unsigned)nCmdLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    // Parse command parameters if present
    if (nCmdLength >= 4)
    {
        u8 param1 = pCmdData[2];
        u8 param2 = pCmdData[3];
        
        logMsg.Format("ISD: CMD 0x17 params: %02X %02X", param1, param2);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
    }
    
    // Return 16-byte response
    memset(pResponse, 0, 16);
    
    // Minimal stub response
    pResponse[0] = 0x00;  // Status: OK
    
    *pResponseLength = 16;
    
    logMsg.Format("ISD: Command 0x17 returning %u bytes", (unsigned)*pResponseLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}

bool ISDProtocol::GetPendingResponseData(u8 *pBuffer, size_t nMaxLength, size_t *pActualLength)
{
    if (m_nResponseDataOffset >= m_nResponseDataLength)
    {
        // No more data
        *pActualLength = 0;
        return false;
    }
    
    size_t remaining = m_nResponseDataLength - m_nResponseDataOffset;
    size_t toSend = (remaining < nMaxLength) ? remaining : nMaxLength;
    
    memcpy(pBuffer, &m_ResponseBuffer[m_nResponseDataOffset], toSend);
    
    // HEX DUMP THE ACTUAL DATA being sent
    CString logMsg;
    logMsg.Format("ISD: Bulk IN sending %u bytes (%u/%u total):", 
                 (unsigned)toSend, (unsigned)(m_nResponseDataOffset + toSend), 
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
            
            // Build ASCII representation
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