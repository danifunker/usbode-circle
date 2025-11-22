//
// isdprotocol.cpp
//
#include "isdprotocol.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>
#include <string.h>

LOGMODULE("isdproto");

// Helper function to log hex data
static void LogHexData(const char *pTitle, const u8 *pData, size_t nLength)
{
    CString msg;
    msg.Format("%s (%u bytes):", pTitle, nLength);
    CLogger::Get()->Write("isdproto", LogNotice, msg);
    
    for (size_t i = 0; i < nLength; i++)
    {
        if (i % 16 == 0)
        {
            if (i > 0)
            {
                CLogger::Get()->Write("isdproto", LogNotice, msg);
            }
            msg.Format("  %04x: %02x", i, pData[i]);
        }
        else
        {
            CString byte;
            byte.Format(" %02x", pData[i]);
            msg.Append(byte);
        }
    }
    if (!msg.GetLength() > 0)
    {
        CLogger::Get()->Write("isdproto", LogNotice, msg);
    }
}

ISDProtocol::ISDProtocol(IImageDevice *pDevice)
    : m_pDevice(pDevice)
{
    memset(&m_playbackState, 0, sizeof(m_playbackState));
    LOGNOTE("ISD Protocol handler initialized");
}

ISDProtocol::~ISDProtocol()
{
}

void ISDProtocol::SetDevice(IImageDevice *pDevice)
{
    m_pDevice = pDevice;
    LOGNOTE("ISD Protocol device updated");
}

bool ISDProtocol::HandleControlTransfer(u8 bmRequestType, u8 bRequest, 
                                        u16 wValue, u16 wIndex, u16 wLength,
                                        u8 *pDataBuffer, size_t *pDataLength)
{
    LOGNOTE("ISD Control Transfer: bmRequestType=%02x bRequest=%02x wValue=%04x wIndex=%04x wLength=%d",
            bmRequestType, bRequest, wValue, wIndex, wLength);
    
    // Check for vendor-specific control transfer (bRequest=6)
    if (bmRequestType == 0x40 && bRequest == 6)
    {
        if (wLength > 0 && pDataBuffer != nullptr)
        {
            // Parse the aa-delimited command batch
            ParseCommandBatch(pDataBuffer, wLength);
            *pDataLength = 0;  // No response data for now
            return true;
        }
    }
    
    LOGWARN("ISD: Unhandled control transfer");
    return false;
}

void ISDProtocol::ParseCommandBatch(const u8 *data, size_t length)
{
    LOGNOTE("ISD: Parsing command batch (%d bytes)", length);
    
    // Log the raw data for debugging
    LogHexData("ISD Batch", data, length);
    
    // Parse aa-delimited commands
    size_t offset = 0;
    while (offset < length)
    {
        // Look for command delimiter (0xAA)
        if (data[offset] == 0xAA)
        {
            // Next byte is command code
            if (offset + 1 < length)
            {
                u8 command = data[offset + 1];
                
                LOGNOTE("ISD: Found command AA %02X at offset %d", command, offset);
                
                // TODO: Parse actual command parameters and length
                
                offset += 2;  // Skip AA and command byte
            }
            else
            {
                break;
            }
        }
        else
        {
            offset++;
        }
    }
    
    LOGWARN("ISD: Command batch parsing not fully implemented");
}

void ISDProtocol::HandleVendorCommand(u8 command, const u8 *params, size_t paramLength)
{
    LOGNOTE("ISD: Vendor command %02X with %d params", command, paramLength);
}

bool ISDProtocol::HandleModeSense(const u8 *pCDB, u8 *pDataBuffer, size_t bufferSize, size_t *pDataLength)
{
    assert(pCDB != nullptr);
    assert(pDataBuffer != nullptr);
    assert(pDataLength != nullptr);
    
    u8 pageCode = pCDB[2] & 0x3F;
    
    LOGNOTE("ISD MODE SENSE: page %02X", pageCode);
    
    if (pageCode == 0x2A)
    {
        return BuildModeSense2A(pDataBuffer, bufferSize, pDataLength);
    }
    else if (pageCode == 0x0E)
    {
        return BuildModeSense0E(pDataBuffer, bufferSize, pDataLength);
    }
    
    LOGWARN("ISD: Unhandled MODE SENSE page %02X", pageCode);
    return false;
}

bool ISDProtocol::BuildModeSense2A(u8 *pDataBuffer, size_t bufferSize, size_t *pDataLength)
{
    LOGNOTE("ISD: Building MODE SENSE 2A response (TOC embedded)");
    
    if (bufferSize < 30)
    {
        LOGERR("ISD: Buffer too small for MODE SENSE 2A");
        return false;
    }
    
    memset(pDataBuffer, 0, 30);
    
    pDataBuffer[0] = 0x00;
    pDataBuffer[1] = 0x1C;
    pDataBuffer[2] = 0x13;
    pDataBuffer[3] = 0x00;
    pDataBuffer[4] = 0x00;
    pDataBuffer[5] = 0x00;
    pDataBuffer[6] = 0x00;
    pDataBuffer[7] = 0x00;
    pDataBuffer[8] = 0x2A;
    pDataBuffer[9]  = 0x14;
    pDataBuffer[10] = 0x07;
    pDataBuffer[11] = 0x07;
    
    *pDataLength = 30;
    
    LOGNOTE("ISD: MODE SENSE 2A response built (%d bytes)", *pDataLength);
    LogHexData("Mode2A", pDataBuffer, *pDataLength);
    
    return true;
}

bool ISDProtocol::BuildModeSense0E(u8 *pDataBuffer, size_t bufferSize, size_t *pDataLength)
{
    LOGNOTE("ISD: Building MODE SENSE 0E response (CD Audio Control)");
    
    if (bufferSize < 16)
    {
        LOGERR("ISD: Buffer too small for MODE SENSE 0E");
        return false;
    }
    
    memset(pDataBuffer, 0, 16);
    
    pDataBuffer[0] = 0x00;
    pDataBuffer[1] = 0x0E;
    pDataBuffer[8] = 0x0E;
    
    *pDataLength = 16;
    
    LOGNOTE("ISD: MODE SENSE 0E response built (%d bytes)", *pDataLength);
    return true;
}

void ISDProtocol::HandlePlayAudio(const u8 *params)
{
    LOGWARN("ISD: PLAY AUDIO not yet implemented");
}

void ISDProtocol::HandlePauseResume(const u8 *params)
{
    LOGWARN("ISD: PAUSE/RESUME not yet implemented");
}

void ISDProtocol::HandleStopPlay(const u8 *params)
{
    LOGWARN("ISD: STOP PLAY not yet implemented");
}

void ISDProtocol::HandleReadSubchannel(const u8 *params)
{
    LOGWARN("ISD: READ SUBCHANNEL not yet implemented");
}