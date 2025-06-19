//
// usbcdgadgetendpoint.cpp
//
// CDROM Gadget by Ian Cass, heavily based on
// USB Mass Storage Gadget by Mike Messinides
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <usbcdgadget/usbcdgadgetendpoint.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h>
#include <assert.h>
#include <circle/sysconfig.h>
#include <circle/util.h>
#include <stddef.h>

#define MLOGNOTE(From,...)		CLogger::Get ()->Write (From, LogNotice, __VA_ARGS__)

CUSBCDGadgetEndpoint::CUSBCDGadgetEndpoint (const TUSBEndpointDescriptor *pDesc,
					      CUSBCDGadget *pGadget)
:	CDWUSBGadgetEndpoint (pDesc, pGadget),
	m_pGadget (pGadget)
{
	assert (pDesc != nullptr);
	assert (pGadget != nullptr);
	
	MLOGNOTE("CDEndpoint", "Endpoint created - Direction: %s", 
		GetDirection() == DirectionOut ? "OUT" : "IN");
}

CUSBCDGadgetEndpoint::~CUSBCDGadgetEndpoint (void)
{
	MLOGNOTE("CDEndpoint", "Endpoint destroyed - Direction: %s", 
		GetDirection() == DirectionOut ? "OUT" : "IN");
	m_pGadget = nullptr;
}

//the following methods forward to the gadget class to facilitate
//unified state management of the device

void CUSBCDGadgetEndpoint::OnActivate (void)
{
	assert (m_pGadget != nullptr);
	
	MLOGNOTE("CDEndpoint", "*** FRAMEWORK ACTIVATION *** Endpoint %s activated by framework", 
		GetDirection() == DirectionOut ? "OUT" : "IN");
	
	// Only trigger activation on OUT endpoint to avoid duplicate activations
	// This follows the pattern where the host initiates communication on the OUT endpoint
	// and follows the Linux gadget pattern of single activation per configuration
	if (GetDirection () == DirectionOut)
	{
		MLOGNOTE("CDEndpoint", "OUT endpoint activated, triggering gadget activation");
		
		// Additional safety check to prevent activation during invalid states
		if (m_pGadget != nullptr && IsValid()) {
			m_pGadget->OnActivate();
		} else {
			MLOGNOTE("CDEndpoint", "skipping activation - gadget or endpoint invalid");
		}
	}
	else
	{
		MLOGNOTE("CDEndpoint", "IN endpoint activated");
	}
}

void CUSBCDGadgetEndpoint::OnTransferComplete (boolean bIn, size_t nLength)
{
	assert (m_pGadget != nullptr);
	
	MLOGNOTE("CDEndpoint","Transfer complete - Direction: %s, Length: %u",
		bIn ? "IN" : "OUT", nLength);
	
	// Additional safety check before forwarding to gadget
	if (m_pGadget != nullptr && IsValid()) {
		m_pGadget->OnTransferComplete(bIn, nLength);
	} else {
		MLOGNOTE("CDEndpoint", "skipping transfer complete - gadget or endpoint invalid");
	}
}

/*
int snprintf (char *buf, size_t size, const char *fmt, ...)
{
        va_list var;
        va_start (var, fmt);

        CString Msg;
        Msg.FormatV (fmt, var);

        va_end (var);

        size_t len = Msg.GetLength ();
        if (--size < len)
        {
                len = size;
        }

        memcpy (buf, (const char *) Msg, len);
        buf[len] = '\0';

        return len;
}

static void HexDumpBuffer(const char* prefix, const void* buffer, size_t length)
{
    const u8* bytes = static_cast<const u8*>(buffer);
    size_t dumpLen = length > 64 ? 64 : length;

    char hexline[3 * 64 + 1] = {0}; // 2 hex chars + 1 space per byte
    char* ptr = hexline;

    for (size_t i = 0; i < dumpLen; ++i)
    {
        // Use snprintf to safely format the byte to hex
        int written = snprintf(ptr, 4, "%02X ", bytes[i]);
        if (written < 0) {
            // Handle error if snprintf fails
            MLOGNOTE("CDEndpoint", "Error formatting byte %d", i);
            return;
        }
        ptr += 3;  // Move pointer for the next byte (2 hex digits + space)
    }

    MLOGNOTE("CDEndpoint", "%s (first %d bytes): %s", prefix, dumpLen, hexline);
}
*/


void CUSBCDGadgetEndpoint::BeginTransfer (TCDTransferMode Mode, void *pBuffer, size_t nLength)
{
	assert (m_pGadget != nullptr);
	assert (pBuffer != nullptr || nLength == 0);
	
	MLOGNOTE("CDEndpoint", "*** HANG CHECK *** BeginTransfer entered - Mode: %d, Length: %u", (int)Mode, nLength);
	
	// Additional safety check before starting transfer
	if (!IsValid()) {
		MLOGNOTE("CDEndpoint", "*** ERROR *** cannot begin transfer - endpoint invalid");
		return;
	}
	
	switch (Mode)
	{
	case TCDTransferMode::TransferCBWOut:
	case TCDTransferMode::TransferDataOut:
		MLOGNOTE("CDEndpoint","*** HANG CHECK *** Begin Transfer OUT - Mode: %s, Length: %u",
			Mode == TCDTransferMode::TransferCBWOut ? "CBW" : "Data", nLength);
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** About to call CDWUSBGadgetEndpoint::BeginTransfer for OUT");
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataOut, pBuffer, nLength);
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** CDWUSBGadgetEndpoint::BeginTransfer OUT completed");
		break;
		
	case TCDTransferMode::TransferDataIn:
	case TCDTransferMode::TransferCSWIn:
		MLOGNOTE("CDEndpoint","*** HANG CHECK *** Begin Transfer IN - Mode: %s, Length: %u",
			Mode == TCDTransferMode::TransferCSWIn ? "CSW" : "Data", nLength);
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** About to call CDWUSBGadgetEndpoint::BeginTransfer for IN");
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataIn, pBuffer, nLength);
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** CDWUSBGadgetEndpoint::BeginTransfer IN completed");
		break;
		
	default:
		MLOGNOTE("CDEndpoint", "*** ERROR *** Invalid transfer mode: %d", (int)Mode);
		assert(0); // Invalid transfer mode
		break;
	}
	
	MLOGNOTE("CDEndpoint", "*** HANG CHECK *** BeginTransfer completed successfully");
}

void CUSBCDGadgetEndpoint::StallRequest(boolean bIn)
{
	assert (m_pGadget != nullptr);
	
	MLOGNOTE("CDEndpoint", "Stalling endpoint - Direction: %s", bIn ? "IN" : "OUT");
	
	// Additional safety check before stalling
	if (IsValid()) {
		CDWUSBGadgetEndpoint::Stall(bIn);
	} else {
		MLOGNOTE("CDEndpoint", "cannot stall - endpoint invalid");
	}
}
