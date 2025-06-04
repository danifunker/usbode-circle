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
#include <circle/usb/gadget/usbcdgadgetendpoint.h>
#include <circle/usb/usb.h>
#include <cueparser/cueparser.h>
#include <discimage/cuebinfile.h>

#ifndef USB_GADGET_DEVICE_ID_CD
#define USB_GADGET_DEVICE_ID_CD        0x1d6b
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

// SCSI Mode Sense(10) Response Structures
struct ModeSense10Header {
    u16 modeDataLength;
    u8  mediumType;
    u8  deviceSpecificParameter;
    u32 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE10_HEADER 8

// Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
struct ModePage0x01Data {
    u8  pageCodeAndPS;
    u8  pageLength;
    u8  errorRecoveryBehaviour;
    u8  readRetryCount;
    u8  reserved[3];
    u8  writeRetryCount;
    u8  reserved2[3];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X01 12

// Mode Page 0x2A (MM Capabilities and Mechanical Status) Data
struct ModePage0x2AData {
    u8  pageCodeAndPS;
    u8  pageLength;
    u8  capabilityBits[6];
    u16 maxSpeed;
    u16 numVolumeLevels;
    u16 bufferSize;
    u16 currentSpeed;
    u8  reserved1[4];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X2A 20

// Mode Page 0x0E (CD Audio Control Page)
struct ModePage0x0EData {
    u8  pageCodeAndPS;
    u8  pageLength;
    u8  IMMEDAndSOTC;
    u8  reserved[5];
    u8  CDDAOutput0Select;
    u8  Output0Volume;
    u8  CDDAOutput1Select;
    u8  Output1Volume;
    u8  CDDAOutput2Select;
    u8  Output2Volume;
    u8  CDDAOutput3Select;
    u8  Output3Volume;
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

// reply to SCSI Read Capacity 0x25
struct TUSBCDReadCapacityReply  // 8 bytes
{
    u32 nLastBlockAddr;
    u32 nSectorSize;
} PACKED;
#define SIZE_READCAPREP 8

struct TUSBCDEventStatusReply  // 8 bytes
{
    u16 eventDataLength;   // 2 bytes: length of remaining data
    u8 notificationClass;  // Media class
    u8 supportedEvents;    // No events supported
    u8 eventClass;         // Media event
    u8 mediaStatus;        // 0x02 = Media present
    u8 reserved[2];        // Reserved
} PACKED;
#define SIZE_EVENT_STATUS_REPLY 8

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

struct TUSBCDGetConfigurationReply {
    u32 dataLength;  // Number of bytes following this field
    u16 reserved;
    u16 currentProfile;  // e.g., 0x0008 for CD-ROM
    u16 profilelist;
    u8 version;
    u8 additionalLength;
    u16 profile;
    u8 current;
    u8 terminator;
} PACKED;
// #define SIZE_GET_CONFIGURATION_REPLY 18
#define SIZE_GET_CONFIGURATION_REPLY 4

struct TUSBCDSubChannelHeaderReply {
	u8 reserved;
	u8 audioStatus;
	u16 dataLength;
	//u8 dataLength1;
	//u8 dataLength2;
} PACKED;
#define SIZE_SUBCHANNEL_HEADER_REPLY 4

struct TUSBCDSubChannel01CurrentPositionReply {
	u8 dataFormatCode; // this should be 0x01
	u8 adrControl; // 0x00 = Q Sub-channel mode information not supplied / 2 audio channels without pre-emphasis
	u8 trackNumber;
	u8 indexNumber;
	u32 absoluteAddress;
	u32 relativeAddress;
} PACKED;
#define SIZE_SUBCHANNEL_01_DATA_REPLY 12


class CUSBCDGadget : public CDWUSBGadget  /// USB mass storage device gadget
{
   public:
    /// \param pInterruptSystem Pointer to the interrupt system object
    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \note pDevice must be initialized yet, when it is specified here.
    /// \note SetDevice() has to be called later, when pDevice is not specified here.
    CUSBCDGadget(CInterruptSystem *pInterruptSystem, CCueBinFileDevice *pDevice = nullptr);

    ~CUSBCDGadget(void);

    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \note Call this, if pDevice has not been specified in the constructor.
    void SetDevice(CCueBinFileDevice *pDevice);

    /// \brief Call this periodically from TASK_LEVEL to allow I/O operations!
    void Update(void);

    /// \param nBlocks Capacity of the block device in number of blocks (a 512 bytes)
    /// \note Used when the block device does not report its size.
    // void SetDeviceBlocks(u64 nBlocks);
    /// \return Capacity of the block device in number of blocks (a 512 bytes)
    // u64 GetBlocks (void) const;

   protected:
    /// \brief Get device-specific descriptor
    /// \param wValue Parameter from setup packet (descriptor type (MSB) and index (LSB))
    /// \param wIndex Parameter from setup packet (e.g. language ID for string descriptors)
    /// \param pLength Pointer to variable, which receives the descriptor size
    /// \return Pointer to descriptor or nullptr, if not available
    /// \note May override this to personalize device.
    const void *GetDescriptor(u16 wValue, u16 wIndex, size_t *pLength) override;

    /// \brief Convert string to UTF-16 string descriptor
    /// \param pString Pointer to ASCII C-string
    /// \param pLength Pointer to variable, which receives the descriptor size
    /// \return Pointer to string descriptor in class-internal buffer
    const void *ToStringDescriptor(const char *pString, size_t *pLength);

   private:
    void AddEndpoints(void) override;

    void CreateDevice(void) override;

    void OnSuspend(void) override;

    int OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData) override;

   private:
    friend class CUSBCDGadgetEndpoint;

    void OnTransferComplete(boolean bIn, size_t nLength);

    void OnActivate();  // called from OUT ep

    void ProcessOut(size_t nLength);

   private:
    void HandleSCSICommand();

    void SendCSW();
    const CUETrackInfo *GetTrackInfoForLBA(u32 lba);
    int GetSkipbytesForTrack(const CUETrackInfo *trackInfo);
    int GetSkipbytes();
    int GetMediumType();
    u32 msf_to_lba(u8 minutes, u8 seconds, u8 frames);

    int GetBlocksize();
    int GetBlocksizeForTrack(const CUETrackInfo *trackInfo);

    void InitDeviceSize(u64 blocks);
    u32 GetLeadoutLBA();
    int GetLastTrackNumber();
    u32 GetAddress(u32 lba, int msf);
    u32 lba_to_msf(u32 lba);

   private:
    CCueBinFileDevice *m_pDevice;

    enum TEPNumber {
        EPIn = 1,
        EPOut = 2,
        NumEPs
    };

    CUSBCDGadgetEndpoint *m_pEP[NumEPs];

    u8 m_StringDescriptorBuffer[80];

   private:
    static const TUSBDeviceDescriptor s_DeviceDescriptor;

    struct TUSBMSTGadgetConfigurationDescriptor {
        TUSBConfigurationDescriptor Configuration;
        TUSBInterfaceDescriptor Interface;
        TUSBEndpointDescriptor EndpointIn;
        TUSBEndpointDescriptor EndpointOut;
    } PACKED;

    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptor;

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

    TUSBCDInquiryReply m_InqReply{0x05, 0x80, 0x02, 0x02, 0x1F, 0, 0, 0, {'U', 'S', 'B', 'O', 'D', 'E', 0, 0}, {'U', 'S', 'B', 'O', 'D', 'E', ' ', 'C', 'D', 'R', 'O', 'M', 0, 0, 0, 0}, {'0', '0', '0', '1'}};
    TUSBUintSerialNumberPage m_InqSerialReply{0x80, 0x00, 0x0000, 0x04, {'0', '0', '0', 0}};

    TUSBSupportedVPDPage m_InqVPDReply{0x00, 0x00, 0x0000, 0x01, 0x80};

    TUSBCDModeSenseReply m_ModeSenseReply{3, 0, 0, 0};
    TUSBCDReadCapacityReply m_ReadCapReply{
        htonl(0x00),  // get's overridden in CUSBCDGadget::InitDeviceSize
        htonl(2048)};

    TUSBCDRequestSenseReply m_ReqSenseReply = {
        0x70,  // current error
        0x00,  // reserved
        0x00,  // Sense Key
               // 0x00: NO SENSE
               // 0x01: RECOVERED ERROR
               // 0x02: NOT READY
               // 0x03: MEDIUM ERROR
               // 0x04: HARDWARE ERROR
               // 0x05: ILLEGAL REQUEST
               // 0x06: UNIT ATTENTION
               // 0x07: DATA PROTECT
               // 0x08: BLANK CHECK
               // 0x09: VENDOR SPECIFIC
               // 0x0A: COPY ABORTED
               // 0x0B: ABORTED COMMAND
               // 0x0D: VOLUME OVERFLOW
               // 0x0E: MISCOMPARE
        {0x0, 0x0,
         0x0, 0x0},  // information
        0x10,        // additional sense length
        {0x0, 0x0,
         0x0, 0x0},      // command specific information
        0x00,            // additional sense code qualifier https://www.t10.org/lists/asc-num.htm
        0x00,            // additional sense code qualifier https://www.t10.org/lists/asc-num.htm
        0x00,            // field replacement unit code
        0x00,            // sksv
        {0x0, 0x0, 0x0}  // sense key specific
    };

    TUSBCDEventStatusReply m_EventStatusReply = {
        htons(0x06),  // Event data length
        0x02,         // Notification class (Media)
        0x00,         // Supported events
        0x02,         // Event class (Media)
        0x02,         // Media status = Present
        {0x00, 0x00}  // Reserved
    };

    // static for now
    TUSBCDReadTOCReply m_TOCReply = {
        htons(0x000A),  // TOC data length (10 bytes follow)
        0x01,           // First track number
        0x01,           // Last track number
        0x00,           // Reserved
        0x14,           // ADR = 1 (LBA), Control = 4 (Data track)
        0x01,           // Track number
        0x00,           // Reserved
        htonl(0x00)     // Track start LBA (big-endian 0)
    };

    TUSBCDGetConfigurationReply m_GetConfigurationReply{
        htonl(0x0C),  // Number of bytes following this field
        htons(0x00),  // Reserved
        htons(0x08),  // Current Profile 0x08 = CDROM
        0x0000,       // Feature: Profile List
        0x03,         // Version: persistent & current
        0x04,         // Additional Length
        htons(0x08),  // Profile 0x08 = CDROM
        0x01,         // Current = true
        0x00          // Reserved
    };

    TUSBDiscInfoReply m_DiscInfoReply{
        htons(0x0020),
        0x01,
        0x01,
        0x01,
        0x01,
        0x01,
        0x00,
        0x00,
        0x00,
        {0x00},
        {0x00},
        {0x00},
        {0x00},
        {0x00}};

    // Must Initialize
    int numTracks = 0;
    // TUSBTOCData m_TOCData;

    static const size_t MaxInMessageSize = 37632;  // 2352 sector * 16 blocks
    static const size_t MaxOutMessageSize = 2048;
    static const size_t MaxSectorSize = 2352;
    u8 *m_FileChunk = new (HEAP_LOW) u8[MaxInMessageSize];
    // u8* m_OneSector = new (HEAP_LOW) u8[MaxSectorSize];
    DMA_BUFFER(u8, m_InBuffer, MaxInMessageSize);
    DMA_BUFFER(u8, m_OutBuffer, MaxOutMessageSize);

    u32 m_nblock_address;
    u32 m_nnumber_blocks;
    // u64 m_nDeviceBlocks=0;
    u32 m_nbyteCount;
    boolean m_CDReady = false;

    CUEParser cueParser;

    u8 bmCSWStatus = 0;
    u8 bSenseKey = 0;
    u8 bAddlSenseCode = 0;
    u8 bAddlSenseCodeQual = 0;
    int data_skip_bytes = 0;
    int data_block_size = 2048;
    int skip_bytes = 0;
    int block_size = 2048;
    int transfer_block_size = 2048;
    int file_mode = 1;
};

#endif
