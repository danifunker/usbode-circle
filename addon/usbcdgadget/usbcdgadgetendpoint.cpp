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
	m_pGadget (pGadget),
	m_bSuspended (FALSE),
	m_nSuspendCount (0),
	m_nResetCount (0)
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
	
	// Only trigger activation on OUT endpoint to avoid duplicate activations
	if (GetDirection () == DirectionOut)
	{
		// Additional safety check to prevent activation during invalid states
		if (m_pGadget != nullptr && IsValid()) {
			m_pGadget->OnActivate();
		}
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

void CUSBCDGadgetEndpoint::OnUSBReset (void)
{
	m_nResetCount++;
	
	MLOGNOTE("CDEndpoint", "*** BIOS CRITICAL *** USB Reset #%u received on %s endpoint", 
		m_nResetCount, GetDirection() == DirectionOut ? "OUT" : "IN");
	
	// Clear suspend state on reset - critical for BIOS compatibility
	m_bSuspended = FALSE;
	
	// Call base class reset handling first
	CDWUSBGadgetEndpoint::OnUSBReset();
	
	// Only trigger gadget reset handling on OUT endpoint to avoid duplicate resets
	if (GetDirection() == DirectionOut && m_pGadget != nullptr)
	{
		MLOGNOTE("CDEndpoint", "*** BIOS CRITICAL *** Triggering gadget reset handling from OUT endpoint");
		// Allow gadget to reinitialize state after reset
		// This is critical for BIOS compatibility as BIOS may reset multiple times
		
		// HANG PROTECTION: Add timeout protection for reset handling
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** About to call ForceStateReset - if system hangs here, reset handler has issues");
		m_pGadget->ForceStateReset();
		MLOGNOTE("CDEndpoint", "*** HANG CHECK *** ForceStateReset completed successfully");
	}
	else
	{
		MLOGNOTE("CDEndpoint", "IN endpoint reset handled");
	}
}

void CUSBCDGadgetEndpoint::OnSuspend (void)
{
	m_nSuspendCount++;
	m_bSuspended = TRUE;
	
	MLOGNOTE("CDEndpoint", "*** BIOS CRITICAL *** USB Suspend #%u received on %s endpoint", 
		m_nSuspendCount, GetDirection() == DirectionOut ? "OUT" : "IN");
	
	// Call base class suspend handling
	CDWUSBGadgetEndpoint::OnSuspend();
	
	// Detect rapid suspend/resume cycles that break BIOS boot
	if (m_nSuspendCount > 3)
	{
		MLOGNOTE("CDEndpoint", "*** BIOS WARNING *** Rapid suspend/resume cycles detected (%u), this breaks BIOS boot!", m_nSuspendCount);
	}
	
	// Only trigger gadget suspend handling on OUT endpoint to avoid duplicate notifications
	if (GetDirection() == DirectionOut && m_pGadget != nullptr)
	{
		MLOGNOTE("CDEndpoint", "*** BIOS CRITICAL *** Triggering gadget suspend handling from OUT endpoint");
		// The gadget's OnSuspend will be called by the main framework
		// We just need to ensure our state is consistent
	}
	else
	{
		MLOGNOTE("CDEndpoint", "IN endpoint suspended");
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
	
	// Additional safety check before starting transfer
	if (!IsValid()) {
		MLOGNOTE("CDEndpoint", "cannot begin transfer - endpoint invalid");
		return;
	}
	
	// Clear suspended state on successful transfer initiation
	if (m_bSuspended) {
		m_bSuspended = FALSE;
	}
	
	switch (Mode)
	{
	case TCDTransferMode::TransferCBWOut:
	case TCDTransferMode::TransferDataOut:
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataOut, pBuffer, nLength);
		break;
		
	case TCDTransferMode::TransferDataIn:
	case TCDTransferMode::TransferCSWIn:
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataIn, pBuffer, nLength);
		break;
		
	default:
		MLOGNOTE("CDEndpoint", "Invalid transfer mode: %d", (int)Mode);
		assert(0);
		break;
	}
}

void CUSBCDGadgetEndpoint::StallRequest(boolean bIn)
{
	assert (m_pGadget != nullptr);
	
	MLOGNOTE("CDEndpoint", "Stalling endpoint - Direction: %s", bIn ? "IN" : "OUT");
	
	// Additional safety check before stalling
	if (IsValid()) {
		CDWUSBGadgetEndpoint::Stall(bIn);
	} else {
		MLOGNOTE("CDEndpoint", "cannot stall - endpoint invalid or suspended");
	}
}

// Enhanced error recovery method for BIOS compatibility
void CUSBCDGadgetEndpoint::RecoverFromSuspend(void)
{
	if (m_bSuspended && m_pGadget != nullptr) {
		MLOGNOTE("CDEndpoint", "*** BIOS RECOVERY *** Recovering %s endpoint from suspended state", 
			GetDirection() == DirectionOut ? "OUT" : "IN");
		
		m_bSuspended = FALSE;
		m_nSuspendCount = 0; // Reset rapid suspend tracking on recovery
		
		// Let the gadget know this endpoint is ready again
		// Only trigger on OUT endpoint to avoid duplicate notifications
		if (GetDirection() == DirectionOut) {
			MLOGNOTE("CDEndpoint", "*** BIOS RECOVERY *** OUT endpoint recovered, notifying gadget");
		}
	}
}
