//
// usbcdgadget.h
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
#ifndef _circle_usb_gadget_usbcdgadget_h
#define _circle_usb_gadget_usbcdgadget_h

#include <circle/device.h>
#include <circle/interrupt.h>
#include <circle/macros.h>
#include <circle/new.h>
#include <circle/synchronize.h>
#include <circle/types.h>
#include <circle/usb/gadget/dwusbgadget.h>
#include <usbcdgadget/usbcdgadgetendpoint.h>
#include <circle/usb/usb.h>
#include <cueparser/cueparser.h>
#include <discimage/cuebinfile.h>

#ifndef USB_GADGET_DEVICE_ID_CD
#define USB_GADGET_DEVICE_ID_CD 0x1d6b
#endif

// If system htonl is not available, define our own
#ifndef HAVE_ARPA_INET_H
static inline u32 htonl(u32 x) {
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8) |
           ((x & 0x00FF0000U) >> 8) |
           ((x & 0xFF000000U) >> 24);
}

static inline u16 htons(u16 x) {
    return ((x & 0x00FFU) << 8) |
           ((x & 0xFF00U) >> 8);
}
#endif

#define LEADOUT_OFFSET 150

struct TUSBCDCBW  // 31 bytes
{
    u32 dCBWSignature;
    u32 dCBWTag;
    u32 dCBWDataTransferLength;
    u8 bmCBWFlags;
    u8 bCBWLUN;
    u8 bCBWCBLength;
    u8 CBWCB[16];
} PACKED;

#define SIZE_CBW 31

#define VALID_CBW_SIG 0x43425355
#define CSW_SIG 0x53425355

struct TUSBCDCSW  // 13 bytes
{
    u32 dCSWSignature = CSW_SIG;
    u32 dCSWTag;
    u32 dCSWDataResidue = 0;
    u8 bmCSWStatus = 0;  // 0=ok 1=command fail 2=phase error
} PACKED;

#define SIZE_CSW 13

#define CD_CSW_STATUS_OK 0
#define CD_CSW_STATUS_FAIL 1
#define CD_CSW_STATUS_PHASE_ERR 2

struct SenseParameters {
    u8 bSenseKey = 0;
    u8 bAddlSenseCode = 0;
    u8 bAddlSenseCodeQual = 0;
};

// reply to SCSI Request Sense Command 0x3
struct TUSBCDRequestSenseReply  // 14 bytes
{
    u8 bErrCode;
    u8 bSegNum;
    u8 bSenseKey;  //=5 command not supported
    u8 bInformation[4];
    u8 bAddlSenseLen;
    u8 bCmdSpecificInfo[4];
    u8 bAddlSenseCode;
    u8 bAddlSenseCodeQual;
    u8 bFieldReplaceUnitCode;
    u8 bSKSVetc;
    u8 sKeySpecific[3];
} PACKED;
#define SIZE_RSR 14

// SCSI Mode Sense(6) Response Structure
struct ModeSense6Header {
    u8 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u8 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE6_HEADER 4

// SCSI Mode Sense(10) Response Structures
struct ModeSense10Header {
    u16 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u32 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE10_HEADER 8

// Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
struct ModePage0x01Data {
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 errorRecoveryBehaviour;
    u8 readRetryCount;
    u8 reserved[3];
    u8 writeRetryCount;
    u8 reserved2[4];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X01 12

// Mode Page 0x1A (Power Condition)
struct ModePage0x1AData {
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 reserved1;
    u8 idleStandby;
    u32 idleConditionTimer;
    u32 standbyConditionTimer;
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X1A 12

// Mode Page 0x2A (MM Capabilities and Mechanical Status) Data
struct ModePage0x2AData {
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 capabilityBits[6];
    u16 maxSpeed;
    u16 numVolumeLevels;
    u16 bufferSize;
    u16 currentSpeed;
    u8 reserved1[4];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X2A 20

// Mode Page 0x0E (CD Audio Control Page)
struct ModePage0x0EData {
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 IMMEDAndSOTC;
    u8 reserved[5];
    u8 CDDAOutput0Select;
    u8 Output0Volume;
    u8 CDDAOutput1Select;
    u8 Output1Volume;
    u8 CDDAOutput2Select;
    u8 Output2Volume;
    u8 CDDAOutput3Select;
    u8 Output3Volume;
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X0E 16

// reply to SCSI Inquiry Command 0x12
struct TUSBCDInquiryReply  // 36 bytes
{
    u8 bPeriphQualDevType;
    u8 bRMB;
    u8 bVersion;
    u8 bRespDataFormatEtc;
    u8 bAddlLength;
    u8 bSCCS;
    u8 bBQUEetc;
    u8 bRELADRetc;
    u8 bVendorID[8];
    u8 bProdID[16];
    u8 bProdRev[4];
} PACKED;
#define SIZE_INQR 36

struct TUSBUintSerialNumberPage {
    u8 PageCode;         // 0x80
    u8 Reserved;         // Reserved
    u16 Reserved2;       // Reserved
    u8 PageLength;       // Length of the Serial Number string
    u8 SerialNumber[4];  // Device Serial Number (ASCII)
} PACKED;
#define SIZE_INQSN 9

struct TUSBSupportedVPDPage {
    u8 PageCode;    // 0x00 for Supported VPD Pages
    u8 Reserved;    // Reserved
    u16 Reserved2;  // Reserved
    u8 PageLength;  // Length of the Supported Page List
    u8 SupportedPageList[1];
} PACKED;
#define SIZE_VPDPAGE 6

// reply to SCSI Mode Sense(6) 0x1A
struct TUSBCDModeSenseReply  // 4 bytes
{
    u8 bModeDataLen;
    u8 bMedType;
    u8 bDevParam;
    u8 bBlockDecrLen;
} PACKED;
#define SIZE_MODEREP 4

// Read Disc Structure header
struct TUSBCDReadDiscStructureHeader
{
    u16 dataLength;
    u8 reserved[2];
} PACKED;

// reply to SCSI Read Capacity 0x25
struct TUSBCDReadCapacityReply  // 8 bytes
{
    u32 nLastBlockAddr;
    u32 nSectorSize;
} PACKED;
#define SIZE_READCAPREP 8

struct TUSBCDEventStatusReplyHeader 
{
    u16 eventDataLength;   // 2 bytes: length of remaining data
    u8 notificationClass;  // Media class
    u8 supportedEventClass;    // No events supported
} PACKED;
#define SIZE_EVENT_STATUS_REPLY_HEADER 4

struct TUSBCDEventStatusReplyEvent
{
	u8 eventCode;
	u8 data[3]; 
} PACKED;
#define SIZE_EVENT_STATUS_REPLY_EVENT 4

struct TUSBCDTrackInformationBlock
{
	u16 dataLength;
	u8 logicalTrackNumberLSB;
	u8 sessionNumberLSB;
	u8 reserved1;
	u8 trackMode;
	u8 dataMode;
	u8 LRANWA;
	u32 logicalTrackStartAddress;
	u32 nextWriteableAddres;
	u32 freeBlocks;
	u32 fixedPacketSize;
	u32 logicalTrackSize;
	u32 lastRecordedAddress;
	u8 logicalTrackNumberMSB;
	u8 sessionNumberMSB;
	u8 reserved2;
	u8 reserved3;
	u32 readCompatibilityLBA;
	u32 nextLayerJumpAddress;
	u32 lastLayerJumpAddress;
} PACKED;


struct TUSBCDReadTOCReply  // 12 bytes
{
    u16 length;  // TOC data length (excluding this field)
    u8 firstTrack;
    u8 lastTrack;

    // Track Descriptor
    u8 reserved;
    u8 adr_ctrl;     // 0x14 = ADR=1 (LBA), Control=4 (Data track)
    u8 trackNumber;  // 1 = Track 1
    u8 reserved2;
    u32 trackStartLBA;  // LBA start of track (e.g. 0)
} PACKED;
#define SIZE_TOC_REPLY 12

struct TUSBTOCEntry {
    u8 reserved = 0x00;
    u8 ADR_Control;
    u8 TrackNumber;
    u8 reserved2 = 0x00;
    u32 address;
} PACKED;
#define SIZE_TOC_ENTRY 8

struct TUSBTOCData {
    u16 DataLength;  // Total length of the TOC data (excluding the length itself)
    u8 FirstTrack;
    u8 LastTrack;
} PACKED;
#define SIZE_TOC_DATA 4

struct TUSBDiscInfoReply {
    u16 data_length;              // Bytes 0–1: Length of remaining data (not including this field), e.g. 0x0020
    u8 disc_status;               // Byte 2: Disc status & erasable flags
    u8 first_track_number;        // Byte 3: First Track Number
    u8 number_of_sessions;        // Byte 4: Number of Sessions
    u8 first_track_last_session;  // Byte 5: First Track Number in Last Session
    u8 last_track_last_session;   // Byte 6: Last Track Number in Last Session
    u8 reserved1;                 // Byte 7: Reserved
    u8 disc_type;                 // Byte 8: Disc Type (e.g. 0 = CD-ROM)
    u8 reserved2;                 // Byte 9: Reserved
    u32 disc_id;                  // Bytes 10–13: Disc Identification (optional, usually zero)
    u32 last_lead_in_start_time;  // Bytes 14–17: Start time of last session's lead-in (optional)
    u32 last_possible_lead_out;   // Bytes 18–21: Last possible lead-out start time
    u8 disc_bar_code[8];          // Bytes 22–29: Disc Bar Code (optional)
    u32 reserved3;                // Bytes 30–33: Reserved / padding
} PACKED;
#define SIZE_DISC_INFO_REPLY 34

struct TUSBCDFeatureHeaderReply {
    u32 dataLength;  // Number of bytes following this field
    u16 reserved;
    u16 currentProfile;  // e.g., 0x0008 for CD-ROM
} PACKED;
#define SIZE_FEATURE_HEADER_REPLY 8

struct TUSBCDProfileListFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;
#define SIZE_PROFILE_LIST_HEADER_REPLY 4

struct TUSBCProfileDescriptorReply {
    u16 profileNumber;
    u8 currentP;
    u8 reserved;
} PACKED;
#define SIZE_PROFILE_DESCRIPTOR_REPLY 4

#define PROFILE_CDROM 0x0008

struct TUSBCDCoreFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u32 physicalInterfaceStandard;
    u8 INQ2DBE;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_CORE_HEADER_REPLY 12

struct TUSBCDMorphingFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 OCEventASYNC;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_MORPHING_HEADER_REPLY 8

struct TUSBCDRemovableMediumFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 Mechanism;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_REMOVABLE_MEDIUM_HEADER_REPLY 8

struct TUSBCDAnalogueAudioPlayFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 ScanSCMSV;
    u8 reserved1;
    u16 NumVolumeLevels;
} PACKED;
#define SIZE_ANALOGUE_AUDIO_PLAY_HEADER_REPLY 8

struct TUSBCDMultiReadFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;
#define SIZE_MULTI_READ_HEADER_REPLY 4

struct TUSBCDPowerManagementFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;
// Note: The define below was SIZE_MULTI_READ_HEADER_REPLY, corrected to SIZE_POWER_MANAGEMENT_HEADER_REPLY
#define SIZE_POWER_MANAGEMENT_HEADER_REPLY 4


struct TUSBCDCDReadFeatureReply {
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 DAPC2FlagsCDText;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_CD_READ_HEADER_REPLY 8 // Corrected from 4, as struct has 8 bytes

struct TUSBCDSubChannelHeaderReply {
    u8 reserved;
    u8 audioStatus;
    u16 dataLength;
} PACKED;
#define SIZE_SUBCHANNEL_HEADER_REPLY 4

struct TUSBCDSubChannel01CurrentPositionReply {
    u8 dataFormatCode;  // this should be 0x01
    u8 adrControl;      // 0x00 = Q Sub-channel mode information not supplied / 2 audio channels without pre-emphasis
    u8 trackNumber;
    u8 indexNumber;
    u32 absoluteAddress;
    u32 relativeAddress;
} PACKED;
#define SIZE_SUBCHANNEL_01_DATA_REPLY 12

struct TUSBCDToolboxFileEntry {
    u8 index;   /* byte 00: file index in directory */
    u8 type;    /* byte 01: type 0 = file, 1 = directory */
    u8 name[33];         /* byte 02-34: filename (32 byte max) + space for NUL terminator */
    u8 size[5]; /* byte 35-39: file size (40 bit big endian unsigned) */
} PACKED;

class CUSBCDGadget : public CDWUSBGadget  /// USB mass storage device gadget
{
   public:
    CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, ICueDevice *pDevice = nullptr);
    ~CUSBCDGadget(void);

    void SetDevice(ICueDevice *pDevice);
    void Update(void);

   protected:
    const void *GetDescriptor(u16 wValue, u16 wIndex, size_t *pLength) override;
    const void *ToStringDescriptor(const char *pString, size_t *pLength);

   private:
    void AddEndpoints(void) override;
    void CreateDevice(void) override;
    void OnSuspend(void) override;
    int OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData) override;

    friend class CUSBCDGadgetEndpoint;
    void OnTransferComplete(boolean bIn, size_t nLength);
    void OnActivate();
    void ProcessOut(size_t nLength);

    void HandleSCSICommand();
    void SendCSW();
    CUETrackInfo GetTrackInfoForLBA(u32 lba);
    CUETrackInfo GetTrackInfoForTrack(int track);
    int GetSkipbytesForTrack(CUETrackInfo trackInfo);
    int GetSkipbytes();
    int GetMediumType();
    u32 msf_to_lba(u8 minutes, u8 seconds, u8 frames);
    static const char *const s_StringDescriptorTemplate[];
    const char *m_StringDescriptor[6]; // Increased size for new strings
    int GetBlocksize();
    int GetBlocksizeForTrack(CUETrackInfo trackInfo);
    void InitDeviceSize(u64 blocks);
    u32 GetLeadoutLBA();
    int GetLastTrackNumber();
    u32 GetAddress(u32 lba, int msf, boolean relative = false);
    u32 lba_to_msf(u32 lba, boolean relative = false);
    int GetSectorLengthFromMCS(uint8_t mainChannelSelection);
    int GetSkipBytesFromMCS(uint8_t mainChannelSelection);

   private:
    ICueDevice *m_pDevice;

    enum TEPNumber {
        EPIn = 1,
        EPOut = 2,
        NumEPs
    };

    CUSBCDGadgetEndpoint *m_pEP[NumEPs];
    u8 m_StringDescriptorBuffer[80];

    static const TUSBDeviceDescriptor s_DeviceDescriptor;

    struct TUSBMSTGadgetConfigurationDescriptor {
        TUSBConfigurationDescriptor Configuration;
        TUSBInterfaceDescriptor Interface;
        TUSBEndpointDescriptor EndpointIn;
        TUSBEndpointDescriptor EndpointOut;
    } PACKED;

    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorFullSpeed;
    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorHighSpeed;
    static const char *const s_StringDescriptor[];

    enum TCDState {
        Init,
        ReceiveCBW,
        InvalidCBW,
        DataIn,
        DataOut,
        SentCSW,
        SendReqSenseReply,
        DataInRead,
        DataOutWrite
    };

    TCDState m_nState = Init;
    TUSBCDCBW m_CBW;
    TUSBCDCSW m_CSW;

    TUSBCDInquiryReply m_InqReply{
	 0x05, 
	 0x80, 
	 0x05, 
	 0x02, 
	 0x1F, 
	 0x00, 
	 0x00, 
	 0x00, 
	 {'L', 'i', 'n', 'u', 'x', ' ', ' ', ' '}, 
	 {'F', 'i', 'l', 'e', '-', 'C', 'D', ' ', 'G', 'a', 'd', 'g', 'e', 't', ' ', ' '}, 
	 {'0', '6', '0', '6'} 
    };
    TUSBUintSerialNumberPage m_InqSerialReply{0x80, 0x00, 0x0000, 0x04, {'0', '0', '0', '0'}};
    TUSBSupportedVPDPage m_InqVPDReply{0x00, 0x00, 0x0000, 0x01, 0x80};
    TUSBCDModeSenseReply m_ModeSenseReply{3, 0, 0, 0};
    TUSBCDReadCapacityReply m_ReadCapReply{
        htonl(0x00), 
        htonl(2048)};
    TUSBCDRequestSenseReply m_ReqSenseReply = {
        0x70, 0x00, 0x00, {0x0, 0x0, 0x0, 0x0}, 0x10, {0x0, 0x0, 0x0, 0x0},
        0x00, 0x00, 0x00, 0x00, {0x0, 0x0, 0x0}  
    };
    TUSBCDReadTOCReply m_TOCReply = {
        htons(0x000A), 0x01, 0x01, 0x00, 0x14, 0x01, 0x00, htonl(0x00)
    };
    TUSBCDFeatureHeaderReply header = {
        htons(0x0000), 0x00, htons(PROFILE_CDROM)
    };
    TUSBCDProfileListFeatureReply profile_list = {
        htons(0x0000), 0x03, 0x04
    };
    TUSBCProfileDescriptorReply cdrom_profile = {
        htons(PROFILE_CDROM), 0x01, 0x00
    };
    TUSBCDCoreFeatureReply core = {
        htons(0x0001), 0x0b, 0x08, 0x08, 0x03, 0x00, 0x00, 0x00
    };
    TUSBCDMorphingFeatureReply morphing = {
        htons(0x0002), 0x0b, 0x04, 0x02, 0x00, 0x00, 0x00
    };
    TUSBCDRemovableMediumFeatureReply mechanism = {
        htons(0x0003), 0x0b, 0x04, 0x15, 0x00, 0x00, 0x00
    };
    TUSBCDAnalogueAudioPlayFeatureReply audioplay = {
    	htons(0x0103), 0x0b, 0x04, 0x00, 0x00, 0xff
    };
    TUSBCDMultiReadFeatureReply multiread = {
        htons(0x001d), 0x0b, 0x00
    };
    TUSBCDPowerManagementFeatureReply powermanagement = {
        htons(0x0100), 0x0b, 0x00
    };
    TUSBCDCDReadFeatureReply cdread = {
        htons(0x001e), 0x0b, 0x04, 0x00, 0x00, 0x00, 0x00
    };
    TUSBDiscInfoReply m_DiscInfoReply{
        htons(0x0020), 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
        0x00, {0x00}, {0x00}, {0x00}, {0x00}, {0x00}
    };

    int numTracks = 0;
    static const size_t MaxOutMessageSize = 2048;
    static const size_t MaxBlocksToRead = 16;
    static const size_t MaxSectorSize = 2352;
    static const size_t MaxInMessageSize = MaxBlocksToRead * MaxSectorSize;
    u8 *m_FileChunk = new u8[MaxInMessageSize];

    DMA_BUFFER(u8, m_InBuffer, MaxInMessageSize);
    DMA_BUFFER(u8, m_OutBuffer, MaxOutMessageSize);

    u32 m_nblock_address;
    u32 m_nnumber_blocks;
    u32 m_nbyteCount;
    boolean m_CDReady = false;
    CUEParser cueParser;
    u8 bmCSWStatus = 0;
    SenseParameters m_SenseParams;
    int data_skip_bytes = 0;
    int data_block_size = 2048;
    int skip_bytes = 0;
    int block_size = 2048;
    int transfer_block_size = 2048;
    int file_mode = 1;
    boolean m_IsFullSpeed = 0;
    boolean discChanged = false;
    uint8_t mcs = 0;
    char m_HardwareSerialNumber[20];

};

#endif
