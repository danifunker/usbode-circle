//
// uscdgadgetendpoint.cpp
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
#include <audioservice/audioservice.h>
#include <circle/sched/scheduler.h>

#define MLOGNOTE(From,...)		//CLogger::Get ()->Write (From, LogNotice, __VA_ARGS__)

CUSBCDGadgetEndpoint::CUSBCDGadgetEndpoint (const TUSBEndpointDescriptor *pDesc,
					      CUSBCDGadget *pGadget)
:	CDWUSBGadgetEndpoint (pDesc, pGadget),
	m_pGadget (pGadget)
{

}

CUSBCDGadgetEndpoint::~CUSBCDGadgetEndpoint (void)
{

}

//the following methods forward to the gadget class to facilitate
//unified state management of the device

void CUSBCDGadgetEndpoint::OnActivate (void)
{
	if (GetDirection () == DirectionOut)
	{
		m_pGadget->OnActivate();
	}

    // Initialize Audio Service here (deferred init)
    CAudioService *pAudio = CAudioService::Get();
    if (pAudio) {
        MLOGNOTE("dwgadget", "Initializing Audio Service after endpoint activation");
        pAudio->Initialize();
    } else {
        MLOGNOTE("dwgadget", "WARNING: Audio Service not found!");
    }
}

void CUSBCDGadgetEndpoint::OnTransferComplete (boolean bIn, size_t nLength)
{
	MLOGNOTE("CDEndpoint","Transfer complete nlen= %i",nLength);
	m_pGadget->OnTransferComplete(bIn, nLength);
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
	switch (Mode)
	{
	case TCDTransferMode::TransferCBWOut:
	case TCDTransferMode::TransferDataOut:
		MLOGNOTE("CDEndpoint","Begin Transfer Out  nlen= %i",nLength);
		//HexDumpBuffer("Outgoing data", pBuffer, nLength);
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataOut, pBuffer, nLength);
		break;
	case TCDTransferMode::TransferDataIn:
	case TCDTransferMode::TransferCSWIn:
		MLOGNOTE("CDEndpoint","Begin Transfer In  nlen= %i",nLength);
		//HexDumpBuffer("Incoming data", pBuffer, nLength);
		CDWUSBGadgetEndpoint::BeginTransfer (TTransferMode::TransferDataIn, pBuffer, nLength);
		break;
	default:
		assert(0);
		break;
	}
}

void CUSBCDGadgetEndpoint::StallRequest(boolean bIn)
{
	CDWUSBGadgetEndpoint::Stall(bIn);

}
