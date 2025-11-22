#include "isdprotocol.h"
#include <circle/logger.h>
#include <circle/util.h>

static const char LogName[] = "isdproto";

ISDProtocol::ISDProtocol(IImageDevice *pDevice)
    : m_pDevice(pDevice),
      m_nLastStatus(0x5B),
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

bool ISDProtocol::HandleControlTransfer(u8 bmRequestType, u8 bRequest,
                                        u16 wValue, u16 wIndex, u16 wLength,
                                        u8 *pData, size_t *pResponseLength)
{
    CString logMsg;
    logMsg.Format("ISD Control Transfer: bmRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%d",
                  bmRequestType, bRequest, wValue, wIndex, wLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);

    // bRequest=5 is STATUS REQUEST (device-to-host)
    if (bmRequestType == 0xC0 && bRequest == 0x05)
    {
        return HandleStatusRequest(pData, pResponseLength);
    }
    
    // Handle different request types (existing code)
    if (bmRequestType == 0x23 && bRequest == 0x02)
    {
        // Class request to interface - likely CLEAR_FEATURE
        *pResponseLength = 0;
        m_nLastStatus = 0x5B;  // Command completed successfully
        return true;
    }
    
    if (bmRequestType == 0x40 && bRequest == 0x04)
    {
        // Vendor command 0x04 - unknown purpose, just ACK
        logMsg.Format("ISD: Vendor command 04 (wValue=%04x) - ACK", wValue);
        CLogger::Get()->Write(LogName, LogNotice, logMsg);
        *pResponseLength = 0;
        m_nLastStatus = 0x5B;  // Command completed
        return true;
    }

    if (bmRequestType == 0x40 && bRequest == 0x06)
    {
        // Vendor command 0x06 - This is the main ISD command batch!
        bool result = HandleCommandBatch(pData, wLength, pResponseLength);
        m_nLastStatus = result ? 0x5B : 0x48;  // Success or error
        return result;
    }

    CLogger::Get()->Write(LogName, LogWarning, "ISD: Unhandled control transfer");
    m_nLastStatus = 0x48;  // Error status
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
        m_nLastStatus = 0x5B;  // Success
    }
    else
    {
        m_nLastStatus = 0x5B;  // Still success even if no response data
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
    pResponse[0] = m_nLastStatus;  // Status byte (0x5B = ready, 0x48 = busy?)
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
    size_t offset = 0;
    size_t responseOffset = 0;
    
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
        CString logMsg;
        logMsg.Format("ISD: Command batch complete (%u response bytes)", (unsigned)responseOffset);
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
        return HandleCommand16(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    case 0x17:  // Unknown vendor command
        return HandleCommand17(pCmdData, nCmdLength, pResponse, pResponseLength);
        
    default:
        logMsg.Format("ISD: Unknown command %02X", commandCode);
        CLogger::Get()->Write(LogName, LogWarning, logMsg);
        return false;
    }
}

bool ISDProtocol::HandleModeSense0E(const u8 *pCmdData, size_t nCmdLength,
                                    u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: MODE SENSE 0x0E (Audio Control)");
    
    // Return minimal audio control page (stubbed for now)
    // Format: page header + audio control parameters
    memset(pResponse, 0, 16);
    pResponse[0] = 0x0E;  // Page code
    pResponse[1] = 0x0E;  // Page length (14 bytes)
    pResponse[2] = 0x05;  // IMMED + SOTC flags
    pResponse[3] = 0x01;  // Output port 0 select (channel 0)
    pResponse[4] = 0xFF;  // Output port 0 volume (max)
    pResponse[5] = 0x02;  // Output port 1 select (channel 1)
    pResponse[6] = 0xFF;  // Output port 1 volume (max)
    
    *pResponseLength = 16;
    return true;
}

bool ISDProtocol::HandleCommand14(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: Command 0x14 (stub)");
    // Return minimal response
    *pResponseLength = 0;
    return true;
}

bool ISDProtocol::HandleCommand15(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: Command 0x15 (stub)");
    // Return minimal response
    *pResponseLength = 0;
    return true;
}

bool ISDProtocol::HandleCommand16(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: Command 0x16 (possibly TOC/capabilities)");
    // Return minimal response
    *pResponseLength = 0;
    return true;
}

bool ISDProtocol::HandleCommand17(const u8 *pCmdData, size_t nCmdLength,
                                  u8 *pResponse, size_t *pResponseLength)
{
    CLogger::Get()->Write(LogName, LogNotice, "ISD: Command 0x17 (stub)");
    // Return minimal response
    *pResponseLength = 0;
    return true;
}

// Stub - remove old MODE SENSE implementation
bool ISDProtocol::HandleModeSense(const u8 *pCDB, u8 *pDataBuffer,
                                 size_t nBufferSize, size_t *pResponseLength)
{
    // This is for SCSI MODE SENSE over bulk endpoints
    // Not used in ISD mode
    return false;
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
    m_nResponseDataOffset += toSend;
    *pActualLength = toSend;
    
    CString logMsg;
    logMsg.Format("ISD: Sending %u bytes on bulk IN (%u/%u total)", 
                 (unsigned)toSend, (unsigned)m_nResponseDataOffset, 
                 (unsigned)m_nResponseDataLength);
    CLogger::Get()->Write(LogName, LogNotice, logMsg);
    
    return true;
}