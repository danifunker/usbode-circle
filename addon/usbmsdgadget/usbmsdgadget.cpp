//
// usbmsdgadget.cpp
//
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
#include <usbmsdgadget/usbmsdgadget.h>
#include <circle/logger.h>
#include <circle/sysconfig.h>
#include <circle/util.h>
#include <assert.h>

#define MLOGNOTE(From,...)		CLogger::Get ()->Write (From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From,...)		//CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From,...)		CLogger::Get ()->Write (From, LogError,__VA_ARGS__)
#define DEFAULT_BLOCKS 16000

const TUSBDeviceDescriptor CUSBMMSDGadget::s_DeviceDescriptor =
{
	sizeof (TUSBDeviceDescriptor),
	DESCRIPTOR_DEVICE,
	0x200,				// bcdUSB
	0,               //bDeviceClass
	0,              //bDeviceSubClass
	0,              //bDeviceProtocol
	64,				// wMaxPacketSize0
	USB_GADGET_VENDOR_ID,
	USB_GADGET_DEVICE_ID_MMSD,
	0x100,				// bcdDevice
	1, 2, 0,			// strings
	1                   //num configurations
};

const CUSBMMSDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBMMSDGadget::s_ConfigurationDescriptorFullSpeed =
{
	{
		sizeof (TUSBConfigurationDescriptor),
		DESCRIPTOR_CONFIGURATION,
		sizeof(TUSBMSTGadgetConfigurationDescriptor),
		1,			// bNumInterfaces
		1,
		0,
		0x80,			// bmAttributes (bus-powered)
		500 / 2			// bMaxPower (500mA)
	},
	{
		sizeof (TUSBInterfaceDescriptor),
		DESCRIPTOR_INTERFACE,
		0,			// bInterfaceNumber
		0,			// bAlternateSetting
		2,			// bNumEndpoints
		0x08, 0x06, 0x50,	// bInterfaceClass, SubClass, Protocol
		0			// iInterface
	},
	{
		sizeof (TUSBEndpointDescriptor),
		DESCRIPTOR_ENDPOINT,
		0x81, 			//IN number 1
		2,			// bmAttributes (Bulk)
		64,			// wMaxPacketSize
		0			// bInterval
	},
	{
		sizeof (TUSBEndpointDescriptor),
		DESCRIPTOR_ENDPOINT,
		0x02, 			//OUT number 2
		2,			// bmAttributes (Bulk)
		64,			// wMaxPacketSize
		0   			// bInterval
	}
};

const CUSBMMSDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBMMSDGadget::s_ConfigurationDescriptorHighSpeed =
{
	{
		sizeof (TUSBConfigurationDescriptor),
		DESCRIPTOR_CONFIGURATION,
		sizeof(TUSBMSTGadgetConfigurationDescriptor),
		1,			// bNumInterfaces
		1,
		0,
		0x80,			// bmAttributes (bus-powered)
		500 / 2			// bMaxPower (500mA)
	},
	{
		sizeof (TUSBInterfaceDescriptor),
		DESCRIPTOR_INTERFACE,
		0,			// bInterfaceNumber
		0,			// bAlternateSetting
		2,			// bNumEndpoints
		0x08, 0x06, 0x50,	// bInterfaceClass, SubClass, Protocol
		0			// iInterface
	},
	{
		sizeof (TUSBEndpointDescriptor),
		DESCRIPTOR_ENDPOINT,
		0x81, 			//IN number 1
		2,			// bmAttributes (Bulk)
		512,			// wMaxPacketSize
		0			// bInterval
	},
	{
		sizeof (TUSBEndpointDescriptor),
		DESCRIPTOR_ENDPOINT,
		0x02, 			//OUT number 2
		2,			// bmAttributes (Bulk)
		512,			// wMaxPacketSize
		0   			// bInterval
	}
};

const char *const CUSBMMSDGadget::s_StringDescriptor[] =
{
	"\x04\x03\x09\x04",		// Language ID
	"Circle",
	"Mass Storage Gadget"
};

CUSBMMSDGadget::CUSBMMSDGadget (CInterruptSystem *pInterruptSystem, boolean isFullSpeed, CDevice *pDevice)
:	CDWUSBGadget (pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
	m_pDevice (pDevice),
	m_pEP {nullptr, nullptr, nullptr}
{
	MLOGNOTE("CUSBMMSDGadget::CUSBMMSDGadget", "entered %d", isFullSpeed);
        m_IsFullSpeed = isFullSpeed;
	if(pDevice)SetDevice(pDevice);
}

CUSBMMSDGadget::~CUSBMMSDGadget (void)
{
	assert (0);
}

const void *CUSBMMSDGadget::GetDescriptor (u16 wValue, u16 wIndex, size_t *pLength)
{
	assert (pLength);

	u8 uchDescIndex = wValue & 0xFF;

	switch (wValue >> 8)
	{
	case DESCRIPTOR_DEVICE:
		if (!uchDescIndex)
		{
			*pLength = sizeof s_DeviceDescriptor;
			return &s_DeviceDescriptor;
		}
		break;

	case DESCRIPTOR_CONFIGURATION:
		if (!uchDescIndex)
		{
			*pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
			return m_IsFullSpeed?&s_ConfigurationDescriptorFullSpeed : &s_ConfigurationDescriptorHighSpeed;
		}
		break;

	case DESCRIPTOR_STRING:
		if (!uchDescIndex)
		{
			*pLength = (u8) s_StringDescriptor[0][0];
			return s_StringDescriptor[0];
		}
		else if (uchDescIndex < sizeof s_StringDescriptor / sizeof s_StringDescriptor[0])
		{
			return ToStringDescriptor (s_StringDescriptor[uchDescIndex], pLength);
		}
		break;

	default:
		break;
	}

	return nullptr;
}

void CUSBMMSDGadget::AddEndpoints (void)
{

	assert (!m_pEP[EPOut]);
	if (m_IsFullSpeed)
            m_pEP[EPOut] = new CUSBMMSDGadgetEndpoint(
                reinterpret_cast<const TUSBEndpointDescriptor*>(
                    &s_ConfigurationDescriptorFullSpeed.EndpointOut),
                this);
        else
            m_pEP[EPOut] = new CUSBMMSDGadgetEndpoint(
                reinterpret_cast<const TUSBEndpointDescriptor*>(
                    &s_ConfigurationDescriptorHighSpeed.EndpointOut),
                this);

	assert (m_pEP[EPOut]);

	assert (!m_pEP[EPIn]);
        if (m_IsFullSpeed)
            m_pEP[EPIn] = new CUSBMMSDGadgetEndpoint(
                reinterpret_cast<const TUSBEndpointDescriptor*>(
                    &s_ConfigurationDescriptorFullSpeed.EndpointIn),
                this);
        else
            m_pEP[EPIn] = new CUSBMMSDGadgetEndpoint(
                reinterpret_cast<const TUSBEndpointDescriptor*>(
                    &s_ConfigurationDescriptorHighSpeed.EndpointIn),
                this);
	assert (m_pEP[EPIn]);

	m_nState=TMMSDState::Init;
}

//must set device before usb activation
void CUSBMMSDGadget::SetDevice (CDevice* dev)
{
	m_pDevice=dev;
	u64 devSize=dev->GetSize();
	//assert(devSize!=(u64)-1);
	if(devSize==(u64)-1)MLOGERR("SetDevice","Device size not reported");
	u64 blocks = devSize==(u64)-1?DEFAULT_BLOCKS:devSize/512;
	InitDeviceSize(blocks);
}

void CUSBMMSDGadget::InitDeviceSize(u64 blocks)
{
	u32 lastBlock=blocks-1;//address of last block
	m_nDeviceBlocks=blocks;
	m_ReadCapReply.nLastBlockAddr= ((lastBlock&0xFF)<<24)|((lastBlock&0xFF00)<<8)
	                               |((lastBlock&0xFF0000)>>8)|((lastBlock&0xFF000000)>>24);
	m_FormatCapReply.numBlocks= ((blocks&0xFF)<<24)|((blocks&0xFF00)<<8)
	                            |((blocks&0xFF0000)>>8)|((blocks&0xFF000000)>>24);
	m_MMSDReady = true;
}

u64 CUSBMMSDGadget::GetBlocks (void) const
{
	return m_nDeviceBlocks;
}

//use when device does not report size
void CUSBMMSDGadget::SetDeviceBlocks(u64 numBlocks)
{
	InitDeviceSize(numBlocks);
}

void CUSBMMSDGadget::CreateDevice (void)
{
	assert(m_pDevice);
}

void CUSBMMSDGadget::OnSuspend (void)
{

	delete m_pEP[EPOut];
	m_pEP[EPOut] = nullptr;

	delete m_pEP[EPIn];
	m_pEP[EPIn] = nullptr;

	m_nState=TMMSDState::Init;
}

const void *CUSBMMSDGadget::ToStringDescriptor (const char *pString, size_t *pLength)
{
	assert (pString);

	size_t nLength = 2;
	for (u8 *p = m_StringDescriptorBuffer+2; *pString; pString++)
	{
		assert (nLength < sizeof m_StringDescriptorBuffer-1);

		*p++ = (u8) *pString;		// convert to UTF-16
		*p++ = '\0';

		nLength += 2;
	}

	m_StringDescriptorBuffer[0] = (u8) nLength;
	m_StringDescriptorBuffer[1] = DESCRIPTOR_STRING;

	assert (pLength);
	*pLength = nLength;

	return m_StringDescriptorBuffer;
}

int CUSBMMSDGadget::OnClassOrVendorRequest (const TSetupData *pSetupData, u8 *pData)
{
	if(pSetupData->bmRequestType==0xA1 && pSetupData->bRequest==0xfe) //get max LUN
	{
		MLOGDEBUG("OnClassOrVendorRequest", "state = %i",m_nState);
		pData[0]=0;
		return 1;
	}
	return -1;
}

void CUSBMMSDGadget::OnTransferComplete (boolean bIn, size_t nLength)
{
	MLOGDEBUG("OnXferComplete", "state = %i, dir = %s, len=%i ",m_nState,bIn?"IN":"OUT",nLength);
	assert(m_nState != TMMSDState::Init);
	if(bIn) //packet to host has been transferred
	{
		switch(m_nState)
		{
		case TMMSDState::SentCSW:
			{
				m_nState=TMMSDState::ReceiveCBW;
				m_pEP[EPOut]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferCBWOut,
				                            m_OutBuffer,SIZE_CBW);
				break;
			}
		case TMMSDState::DataIn:
			{
				if(m_nnumber_blocks>0)
				{
					if(m_MMSDReady)
					{
						m_nState=TMMSDState::DataInRead; //see Update function
					}
					else
					{
						MLOGERR("onXferCmplt DataIn","failed, %s",
						        m_MMSDReady?"ready":"not ready");
						m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
						m_ReqSenseReply.bSenseKey = 2;
						m_ReqSenseReply.bAddlSenseCode = 1;
						SendCSW();
					}
				}
				else     //done sending data to host
				{
					SendCSW();
				}
				break;
			}
		case TMMSDState::SendReqSenseReply:
			{
				SendCSW();
				break;
			}
		default:
			{
				MLOGERR("onXferCmplt","dir=in, unhandled state = %i", m_nState);
				assert(0);
				break;
			}
		}
	}
	else    //packet from host is available in m_OutBuffer
	{
		switch(m_nState)
		{
		case TMMSDState::ReceiveCBW:
			{
				if(nLength != SIZE_CBW)
				{
					MLOGERR("ReceiveCBW","Invalid CBW len = %i",nLength);
					m_pEP[EPIn]->StallRequest(true);
					break;
				}
				memcpy(&m_CBW,m_OutBuffer,SIZE_CBW);
				if(m_CBW.dCBWSignature != VALID_CBW_SIG)
				{
					MLOGERR("ReceiveCBW","Invalid CBW sig = 0x%x",
						m_CBW.dCBWSignature);
					m_pEP[EPIn]->StallRequest(true);
					break;
				}
				m_CSW.dCSWTag=m_CBW.dCBWTag;
				if(m_CBW.bCBWCBLength<=16 && m_CBW.bCBWLUN==0) //meaningful CBW
				{
					HandleSCSICommand(); //will update m_nstate
					break;
				} // TODO: response for not meaningful CBW
				break;
			}
		case TMMSDState::DataOut:
			{
				//process block from host
				assert(m_nnumber_blocks>0);
				if(m_MMSDReady)
				{
					m_nState=TMMSDState::DataOutWrite; //see Update function
				}
				else
				{
					MLOGERR("onXferCmplt DataOut","failed, %s",
					        m_MMSDReady?"ready":"not ready");
					m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
					m_ReqSenseReply.bSenseKey = 2;
					m_ReqSenseReply.bAddlSenseCode = 1;
					SendCSW();
				}
				break;
			}

		default:
			{
				MLOGERR("onXferCmplt","dir=out, unhandled state = %i", m_nState);
				assert(0);
				break;
			}
		}
	}
}

// will be called before vendor request 0xfe
void CUSBMMSDGadget::OnActivate()
{
	MLOGNOTE("MMSD OnActivate", "state = %i",m_nState);
	m_MMSDReady=true;
	m_nState=TMMSDState::ReceiveCBW;
	m_pEP[EPOut]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferCBWOut,m_OutBuffer,SIZE_CBW);
}

void CUSBMMSDGadget::OnDeactivate (void)
{
    m_nState = Init;
    m_MMSDReady = false;
}

void CUSBMMSDGadget::SendCSW()
{
	memcpy(&m_InBuffer,&m_CSW,SIZE_CSW);
	m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferCSWIn,m_InBuffer,SIZE_CSW);
	m_nState=TMMSDState::SentCSW;
}

void CUSBMMSDGadget::HandleSCSICommand()
{
	switch(m_CBW.CBWCB[0])
	{
	case 0x0: // Test unit ready
		{
			m_CSW.bmCSWStatus=m_MMSDReady?MMSD_CSW_STATUS_OK:MMSD_CSW_STATUS_FAIL;
			if(!m_MMSDReady)
			{
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
				m_ReqSenseReply.bSenseKey = 2;
				m_ReqSenseReply.bAddlSenseCode = 1;
			}
			else
			{
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
				m_ReqSenseReply.bSenseKey = 0;
				m_ReqSenseReply.bAddlSenseCode = 0;
			}
			SendCSW();
			break;
		}
	case 0x3: // Request sense CMD
		{
			memcpy(&m_InBuffer,&m_ReqSenseReply,SIZE_RSR);
			m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn,
			                           m_InBuffer,SIZE_RSR);
			m_nState=TMMSDState::SendReqSenseReply;
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			break;
		}
	case 0x12: // Inquiry
		{
			memcpy(&m_InBuffer,&m_InqReply,SIZE_INQR);
			m_nnumber_blocks=0; //nothing more after this send
			m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn,
			                           m_InBuffer,SIZE_INQR);
			m_nState=TMMSDState::DataIn;
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			break;
		}
	case 0x1A: // Mode sense (6)
		{
			memcpy(&m_InBuffer,&m_ModeSenseReply,SIZE_MODEREP);
			m_nnumber_blocks=0; //nothing more after this send
			m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn,
			                           m_InBuffer,SIZE_MODEREP);
			m_nState=TMMSDState::DataIn;
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			break;
		}
	case 0x1B: // Start/stop unit
		{
			m_MMSDReady = (m_CBW.CBWCB[4] >> 1) == 0;
			MLOGNOTE("HandleSCSI","start/stop, %s",m_MMSDReady?"ready":"not ready");
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			SendCSW();
			break;
		}
	case 0x1E: // allow removal
		{
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
			m_ReqSenseReply.bSenseKey = 0x5; // Illegal/not supported
			m_ReqSenseReply.bAddlSenseCode = 0x20;
			SendCSW();
			break;
		}

	case 0x23: // format capacity
		{
			memcpy(&m_InBuffer,&m_FormatCapReply,SIZE_FORMATR);
			m_nnumber_blocks=0; //nothing more after this send
			m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn,
			                           m_InBuffer,SIZE_FORMATR);
			m_nState=TMMSDState::DataIn;
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			break;
		}
	case 0x25: // Read capacity (10))
		{
			memcpy(&m_InBuffer,&m_ReadCapReply,SIZE_READCAPREP);
			m_nnumber_blocks=0; //nothing more after this send
			m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn,
			                           m_InBuffer,SIZE_READCAPREP);
			m_nState=TMMSDState::DataIn;
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			break;
		}
	case 0x28: // Read (10)
		{
			if(m_MMSDReady)
			{
				//will be updated if read fails on any block
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;

				m_ReqSenseReply.bSenseKey = 0;
				m_ReqSenseReply.bAddlSenseCode = 0;
				m_nnumber_blocks = (u32)((m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8]);
				m_nblock_address =   (u32)(m_CBW.CBWCB[2] << 24)
				                   | (u32)(m_CBW.CBWCB[3] << 16)
				                   | (u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
				m_nbyteCount=m_CBW.dCBWDataTransferLength;
				if(m_nnumber_blocks==0)
				{
					m_nnumber_blocks=1+(m_nbyteCount)/BLOCK_SIZE;
				}
				MLOGDEBUG("Read(10)","addr = %u len = %u",
					  m_nblock_address,m_nnumber_blocks);
				m_nState=TMMSDState::DataInRead; //see Update() function
			}
			else
			{
				MLOGERR("handleSCSI Read(10)","failed, %s",
					m_MMSDReady?"ready":"not ready");
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
				m_ReqSenseReply.bSenseKey = 2;
				m_ReqSenseReply.bAddlSenseCode = 1;
				SendCSW();
			}
			break;
		}

	case 0x2A: // Write (10)
		{
			if(m_MMSDReady)
			{
				//->big endian
				m_nnumber_blocks = (u32)(m_CBW.CBWCB[7] << 8) | m_CBW.CBWCB[8];
				m_nblock_address = (u32)(m_CBW.CBWCB[2] << 24) | (u32)(m_CBW.CBWCB[3] << 16)
				                   |(u32)(m_CBW.CBWCB[4] << 8) | m_CBW.CBWCB[5];
				MLOGDEBUG("Write(10)","addr = %u len = %u",m_nblock_address,m_nnumber_blocks);

				m_nnumber_blocks_chunk = (m_nnumber_blocks > 16) ? 16 : m_nnumber_blocks;

				m_pEP[EPOut]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataOut,
				                            m_OutBuffer, BLOCK_SIZE * m_nnumber_blocks_chunk);
				m_nState=TMMSDState::DataOut;
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;	   //will be updated if write fails
				m_ReqSenseReply.bSenseKey = 0;
				m_ReqSenseReply.bAddlSenseCode = 0;
			}
			else
			{
				MLOGERR("handleSCSI write(10)","failed, %s",
					m_MMSDReady?"ready":"not ready");
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
				m_ReqSenseReply.bSenseKey = 2;
				m_ReqSenseReply.bAddlSenseCode = 1;
				SendCSW();
			}
			break;
		}

	case 0x2F: // Verify, not implemented but don't tell host
		{
			m_CSW.bmCSWStatus=MMSD_CSW_STATUS_OK;
			m_ReqSenseReply.bSenseKey = 0;
			m_ReqSenseReply.bAddlSenseCode = 0;
			SendCSW();
			break;
		}

	default:
		{
			m_ReqSenseReply.bSenseKey = 0x5; // Illegal/not supported
			m_ReqSenseReply.bAddlSenseCode = 0x20;
			m_CSW.bmCSWStatus = MMSD_CSW_STATUS_FAIL;
			SendCSW();
			break;
		}

	}
}

//this function is called periodically from task level for IO
//(IO must not be attempted in functions called from IRQ)
void CUSBMMSDGadget::Update()
{
	switch(m_nState)
	{
	case TMMSDState::DataInRead:
		{
			u64 offset=0;

			if (m_MMSDReady) {
				// TODO: Try to avoid seek
			    // Seek to the correct block address
			    if (m_pDevice->Seek(BLOCK_SIZE * m_nblock_address) != (u64)-1) {
				// Determine the number of blocks to read, capping at a maximum of 16
				u32 blocks_to_read = (m_nnumber_blocks > 16) ? 16 : m_nnumber_blocks;
				u32 bytes_to_read = blocks_to_read * BLOCK_SIZE;

				MLOGDEBUG("UpdateRead", "Attempting to read %u blocks (%u bytes) starting at block %lu",
					  blocks_to_read, bytes_to_read, m_nblock_address);

				// Read directly into the input buffer
				int read_count = m_pDevice->Read(m_InBuffer, bytes_to_read);

				if (read_count != static_cast<int>(bytes_to_read)) {
				    // Handle a failed or partial read
				    MLOGERR("UpdateRead", "Read error: expected %u bytes, got %d", bytes_to_read, read_count);
				    m_CSW.bmCSWStatus = MMSD_CSW_STATUS_FAIL;
					m_ReqSenseReply.bSenseKey = 0x2;
					m_ReqSenseReply.bAddlSenseCode = 0x1;
				    SendCSW();
				    return;
				}

				// Update counts and state for the next operation
				m_nblock_address += blocks_to_read;
				m_nnumber_blocks -= blocks_to_read;
				m_nbyteCount -= bytes_to_read;
				m_nState = TMMSDState::DataIn;

				MLOGDEBUG("UpdateRead", "Read successful. Remaining blocks: %lu", m_nnumber_blocks);

				// Initiate the USB transfer with the data read
				m_pEP[EPIn]->BeginTransfer(CUSBMMSDGadgetEndpoint::TransferDataIn, m_InBuffer, bytes_to_read);
			    }
			}

			if(!m_MMSDReady || offset==(u64)(-1))
			{
				MLOGERR("UpdateRead","failed, %s, offset=%i",
				        m_MMSDReady?"ready":"not ready",offset);
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
				m_ReqSenseReply.bSenseKey = 2;
				m_ReqSenseReply.bAddlSenseCode = 1;
				SendCSW();
			}
			break;
		}
	case TMMSDState::DataOutWrite:
		{
			//process block from host
			assert(m_nnumber_blocks>0);
			u64 offset=0;
			int writeCount=0;
			if(m_MMSDReady)
			{
				// Try to avoid seeking
				u64 desired_file_position = (BLOCK_SIZE * m_nblock_address);
				if (m_currentDevicePointer != desired_file_position) {
					offset=m_pDevice->Seek(desired_file_position);
					m_currentDevicePointer = desired_file_position;
				} 

				// Try to write all 16 blocks
				int write_length = BLOCK_SIZE * m_nnumber_blocks_chunk;
				if(offset!=(u64)(-1))
				{
					writeCount=m_pDevice->Write(m_OutBuffer, write_length);
				}
				if(writeCount>0)
				{
					if(writeCount < write_length)
					{
						MLOGERR("UpdateWrite","writeCount = %u ",writeCount);
						m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
						m_ReqSenseReply.bSenseKey = 0x2;
						m_ReqSenseReply.bAddlSenseCode = 0x1;
						SendCSW();
						break;
					}
					m_nnumber_blocks -= m_nnumber_blocks_chunk;
					m_nblock_address += m_nnumber_blocks_chunk;
					m_currentDevicePointer += writeCount;
					if(m_nnumber_blocks==0)  //done receiving data from host
					{
						SendCSW();
						break;
					}
				}
			}

			if(!m_MMSDReady || offset==(u64)(-1) || writeCount<=0)
			{
				MLOGERR("UpdateWrite","failed, %s, offset=%i, writeCount=%i",
				        m_MMSDReady?"ready":"not ready",offset,writeCount);
				m_CSW.bmCSWStatus=MMSD_CSW_STATUS_FAIL;
				m_ReqSenseReply.bSenseKey = 2;
				m_ReqSenseReply.bAddlSenseCode = 1;
				SendCSW();
				break;
			}
			else
			{
				if(m_nnumber_blocks>0)  //get next block
				{
					m_nnumber_blocks_chunk = (m_nnumber_blocks > 16) ? 16 : m_nnumber_blocks;

					m_pEP[EPOut]->BeginTransfer(
						CUSBMMSDGadgetEndpoint::TransferDataOut,
					        m_OutBuffer, BLOCK_SIZE * m_nnumber_blocks_chunk);
					m_nState=TMMSDState::DataOut;
				}
			}
			break;
		}
	default:
		break;
	}
}
