//
// usbcdgadget.h
//
// CDROM Gadget by Ian Cass, and Dani Sarfati heavily based on
// USB Mass Storage Gadget by Mike Messinides and BlueSCSI v2
// https://github.com/BlueSCSI/BlueSCSI-v2
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

// ============================================================================
// Network Byte Order Helpers
// ============================================================================

#ifndef HAVE_ARPA_INET_H
static inline u32 htonl(u32 x)
{
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8) |
           ((x & 0x00FF0000U) >> 8) |
           ((x & 0xFF000000U) >> 24);
}

static inline u16 htons(u16 x)
{
    return ((x & 0x00FFU) << 8) |
           ((x & 0xFF00U) >> 8);
}
#endif

// ============================================================================
// CD-ROM Constants
// ============================================================================

#define LEADOUT_OFFSET 150

// Profile codes (MMC-3)
#define PROFILE_CDROM 0x0008
#define PROFILE_DVD_ROM 0x0010

// ============================================================================
// USB Bulk-Only Transport (BOT) Structures
// ============================================================================

struct TUSBCDCBW // Command Block Wrapper - 31 bytes
{
    u32 dCBWSignature;          // 'USBC' = 0x43425355
    u32 dCBWTag;                // Command tag
    u32 dCBWDataTransferLength; // Transfer length
    u8 bmCBWFlags;              // Direction flags
    u8 bCBWLUN;                 // Logical unit number
    u8 bCBWCBLength;            // Command block length
    u8 CBWCB[16];               // Command block
} PACKED;

#define SIZE_CBW 31
#define VALID_CBW_SIG 0x43425355
#define CSW_SIG 0x53425355
struct TUSBCDCSW // Command Status Wrapper - 13 bytes
{
    u32 dCSWSignature = CSW_SIG; // 'USBS' = 0x53425355
    u32 dCSWTag;                 // Command tag (matches CBW)
    u32 dCSWDataResidue = 0;     // Residue count
    u8 bmCSWStatus = 0;          // Status: 0=OK, 1=Fail, 2=Phase Error
} PACKED;

#define SIZE_CSW 13
#define CD_CSW_STATUS_OK 0
#define CD_CSW_STATUS_FAIL 1
#define CD_CSW_STATUS_PHASE_ERR 2

// ============================================================================
// SCSI Sense Data
// ============================================================================

struct SenseParameters
{
    u8 bSenseKey = 0;
    u8 bAddlSenseCode = 0;
    u8 bAddlSenseCodeQual = 0;
};

// ============================================================================
// SCSI Command Reply Structures
// ============================================================================

// REQUEST SENSE (0x03) - 14 bytes
struct TUSBCDRequestSenseReply
{
    u8 bErrCode;            // Error code (0x70 = current, 0x71 = deferred)
    u8 bSegNum;             // Segment number
    u8 bSenseKey;           // Sense key (see MMC-3 spec)
    u8 bInformation[4];     // Information bytes
    u8 bAddlSenseLen;       // Additional sense length (0x0A for fixed format)
    u8 bCmdSpecificInfo[4]; // Command-specific info
    u8 bAddlSenseCode;      // ASC - Additional Sense Code
    u8 bAddlSenseCodeQual;  // ASCQ - Additional Sense Code Qualifier
    u8 bFieldReplaceUnitCode;
    u8 bSKSVetc;
    u8 sKeySpecific[3];
} PACKED;
#define SIZE_RSR 14

// INQUIRY (0x12) - 96 bytes
struct TUSBCDInquiryReply
{
    u8 bPeriphQualDevType;      // Byte 0: Peripheral qualifier + device type
    u8 bRMB;                    // Byte 1: Removable media bit
    u8 bVersion;                // Byte 2: SCSI version
    u8 bRespDataFormatEtc;      // Byte 3: Response data format
    u8 bAddlLength;             // Byte 4: Additional length
    u8 bSCCS;                   // Byte 5: SCCS bits
    u8 bBQUEetc;                // Byte 6: BQUE, ENCSERV, etc.
    u8 bRELADRetc;              // Byte 7: RELADR, etc.
    u8 bVendorID[8];            // Bytes 8-15: Vendor ID
    u8 bProdID[16];             // Bytes 16-31: Product ID
    u8 bProdRev[4];             // Bytes 32-35: Product revision
    u8 bVendorSpecific[20];     // Bytes 36-55: Vendor specific
    u8 bReserved[2];            // Bytes 56-57: Reserved
    u8 bVersionDescriptors[16]; // Bytes 58-73: Version descriptors
    u8 bReserved2[22];          // Bytes 74-95: Reserved/padding
} PACKED;
#define SIZE_INQR 96

// INQUIRY VPD Page 0x80 - Unit Serial Number
struct TUSBUintSerialNumberPage
{
    u8 PageCode; // 0x80
    u8 Reserved;
    u16 Reserved2;
    u8 PageLength;      // Length of serial number
    u8 SerialNumber[4]; // Device serial number (ASCII)
} PACKED;
#define SIZE_INQSN 9

// INQUIRY VPD Page 0x00 - Supported VPD Pages
struct TUSBSupportedVPDPage
{
    u8 PageCode; // 0x00
    u8 Reserved;
    u16 Reserved2;
    u8 PageLength;
    u8 SupportedPageList[1];
} PACKED;
#define SIZE_VPDPAGE 6

// MODE SENSE(6) (0x1A) - Header
struct ModeSense6Header
{
    u8 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u8 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE6_HEADER 4

struct TUSBCDModeSenseReply // Simplified header
{
    u8 bModeDataLen;
    u8 bMedType;
    u8 bDevParam;
    u8 bBlockDecrLen;
} PACKED;
#define SIZE_MODEREP 4

// MODE SENSE(10) (0x5A) - Header
struct ModeSense10Header
{
    u16 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u16 reserved;
    u16 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE10_HEADER 8

// Mode Page 0x01 - Read/Write Error Recovery
struct ModePage0x01Data
{
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 errorRecoveryBehaviour;
    u8 readRetryCount;
    u8 reserved[3];
    u8 writeRetryCount;
    u8 reserved2[4];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X01 12

// Mode Page 0x0E - CD Audio Control
struct ModePage0x0EData
{
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

// Mode Page 0x1A - Power Condition
struct ModePage0x1AData
{
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 reserved1;
    u8 idleStandby;
    u32 idleConditionTimer;
    u32 standbyConditionTimer;
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X1A 12

// Mode Page 0x2A - MM Capabilities and Mechanical Status
struct ModePage0x2AData
{
    u8 pageCodeAndPS;
    u8 pageLength;
    u8 capabilityBits[6];
    u16 maxSpeed;
    u16 numVolumeLevels;
    u16 bufferSize;
    u16 currentSpeed;
    u8 reserved1[4];
    u16 maxReadSpeed;
    u8 reserved2[2];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X2A 20

// READ CAPACITY (0x25) - 8 bytes
struct TUSBCDReadCapacityReply
{
    u32 nLastBlockAddr; // Last logical block address
    u32 nSectorSize;    // Block size in bytes
} PACKED;
#define SIZE_READCAPREP 8

// READ TOC (0x43) - Format 0 response
struct TUSBCDReadTOCReply
{
    u16 length;    // TOC data length (excluding this field)
    u8 firstTrack; // First track number
    u8 lastTrack;  // Last track number
    u8 reserved;
    u8 adr_ctrl;    // ADR/Control byte
    u8 trackNumber; // Track number
    u8 reserved2;
    u32 trackStartLBA; // Track start address
} PACKED;
#define SIZE_TOC_REPLY 12

// READ TOC - Track descriptor entry
struct TUSBTOCEntry
{
    u8 reserved;
    u8 ADR_Control;
    u8 TrackNumber;
    u8 reserved2;
    u32 address;
} PACKED;
#define SIZE_TOC_ENTRY 8

// READ TOC - TOC header
struct TUSBTOCData
{
    u16 DataLength; // Length of remaining data
    u8 FirstTrack;
    u8 LastTrack;
} PACKED;
#define SIZE_TOC_DATA 4

// READ SUB-CHANNEL (0x42) - Header
struct TUSBCDSubChannelHeaderReply
{
    u8 reserved;
    u8 audioStatus; // Audio playback status
    u16 dataLength; // Remaining data length
} PACKED;
#define SIZE_SUBCHANNEL_HEADER_REPLY 4

// READ SUB-CHANNEL - Format 0x01 (Current Position)
struct TUSBCDSubChannel01CurrentPositionReply
{
    u8 dataFormatCode;   // 0x01
    u8 adrControl;       // ADR/Control
    u8 trackNumber;      // Current track
    u8 indexNumber;      // Current index
    u32 absoluteAddress; // Absolute CD address
    u32 relativeAddress; // Track-relative address
} PACKED;
#define SIZE_SUBCHANNEL_01_DATA_REPLY 12

// READ HEADER (0x44)
struct TUSBCDReadDiscStructureHeader
{
    u16 dataLength;
    u8 reserved[2];
} PACKED;

// READ TRACK INFORMATION (0x52)
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

// READ DISC INFORMATION (0x51)
struct TUSBDiscInfoReply
{
    u16 data_length; // Length of remaining data
    u8 disc_status;  // Disc status
    u8 first_track_number;
    u8 number_of_sessions;
    u8 first_track_last_session;
    u8 last_track_last_session;
    u8 reserved1;
    u8 disc_type; // Disc type
    u8 reserved2;
    u32 disc_id;
    u32 last_lead_in_start_time;
    u32 last_possible_lead_out;
    u8 disc_bar_code[8];
    u32 reserved3;
} PACKED;
#define SIZE_DISC_INFO_REPLY 34

// GET EVENT STATUS NOTIFICATION (0x4A)
struct TUSBCDEventStatusReplyHeader
{
    u16 eventDataLength;
    u8 notificationClass;
    u8 supportedEventClass;
} PACKED;
#define SIZE_EVENT_STATUS_REPLY_HEADER 4

struct TUSBCDEventStatusReplyEvent
{
    u8 eventCode;
    u8 data[3];
} PACKED;
#define SIZE_EVENT_STATUS_REPLY_EVENT 4

// GET CONFIGURATION (0x46) - Feature descriptors

struct TUSBCDFeatureHeaderReply
{
    u32 dataLength; // Length of remaining data
    u16 reserved;
    u16 currentProfile; // Current profile (e.g., 0x0008 for CD-ROM)
} PACKED;
#define SIZE_FEATURE_HEADER_REPLY 8

struct TUSBCDProfileListFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;
#define SIZE_PROFILE_LIST_HEADER_REPLY 4

struct TUSBCProfileDescriptorReply
{
    u16 profileNumber;
    u8 currentP; // Current profile flag
    u8 reserved;
} PACKED;
#define SIZE_PROFILE_DESCRIPTOR_REPLY 4

struct TUSBCDCoreFeatureReply
{
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

struct TUSBCDMorphingFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 OCEventASYNC;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_MORPHING_HEADER_REPLY 8

struct TUSBCDRemovableMediumFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 Mechanism;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_REMOVABLE_MEDIUM_HEADER_REPLY 8

struct TUSBCDAnalogueAudioPlayFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 ScanSCMSV;
    u8 reserved1;
    u16 NumVolumeLevels;
} PACKED;
#define SIZE_ANALOGUE_AUDIO_PLAY_HEADER_REPLY 8

struct TUSBCDMultiReadFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;
#define SIZE_MULTI_READ_HEADER_REPLY 4

struct TUSBCDPowerManagementFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
} PACKED;

struct TUSBCDCDReadFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 DAPC2FlagsCDText;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_CD_READ_HEADER_REPLY 4

struct TUSBCDDVDReadFeatureReply
{
    u16 featureCode;
    u8 VersionPersistentCurrent;
    u8 AdditionalLength;
    u8 MultiUnitsDUALLayerBuff;
    u8 reserved1;
    u8 reserved2;
    u8 reserved3;
} PACKED;
#define SIZE_DVD_READ_HEADER_REPLY 8

// ============================================================================
// Vendor-Specific Toolbox Commands
// ============================================================================

struct TUSBCDToolboxFileEntry
{
    u8 index;    // File index in directory
    u8 type;     // 0 = file, 1 = directory
    u8 name[33]; // Filename (32 bytes + NUL terminator)
    u8 size[5];  // File size (40-bit big-endian)
} PACKED;

// ============================================================================
// Main USB CD-ROM Gadget Class
// ============================================================================

class CUSBCDGadget : public CDWUSBGadget
{
public:
    /// \brief Constructor
    /// \param pInterruptSystem Pointer to interrupt system
    /// \param isFullSpeed True for USB 1.1 full-speed (12 Mbps), false for USB 2.0 high-speed (480 Mbps)
    /// \param pDevice Optional CUE-aware device pointer (can be set later via SetDevice)
    CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, ICueDevice *pDevice = nullptr);

    ~CUSBCDGadget(void);

    /// \brief Set the CUE-aware block device to be exposed via USB
    /// \param pDevice Pointer to device implementing ICueDevice interface
    /// \note Call this if device wasn't provided in constructor
    void SetDevice(ICueDevice *pDevice);

    /// \brief Process USB transfers - must be called periodically from TASK_LEVEL
    /// \note This processes the SCSI command state machine and data transfers
    void Update(void);

protected:
    // ========================================================================
    // CDWUSBGadget Overrides
    // ========================================================================

    /// \brief Get device-specific USB descriptor
    /// \param wValue Descriptor type (MSB) and index (LSB) from setup packet
    /// \param wIndex Language ID or other index from setup packet
    /// \param pLength Receives the descriptor size
    /// \return Pointer to descriptor or nullptr if not available
    const void *GetDescriptor(u16 wValue, u16 wIndex, size_t *pLength) override;

    /// \brief Add bulk endpoints for data transfer
    void AddEndpoints(void) override;

    /// \brief Create and configure USB device
    void CreateDevice(void) override;

    /// \brief Handle USB bus suspend event
    void OnSuspend(void) override;

    /// \brief Handle class or vendor-specific control requests
    /// \param pSetupData Setup packet data
    /// \param pData Data buffer for request
    /// \return Number of bytes for data phase, 0 for no data, -1 for STALL
    int OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData) override;

private:
    // ========================================================================
    // USB Transfer Callbacks (called from IRQ level via CUSBCDGadgetEndpoint)
    // ========================================================================
    friend class CUSBCDGadgetEndpoint;

    /// \brief Handle transfer completion
    /// \param bIn True for IN transfer, false for OUT transfer
    /// \param nLength Number of bytes transferred
    void OnTransferComplete(boolean bIn, size_t nLength);

    /// \brief Handle USB activation event
    void OnActivate();

    /// \brief Process received OUT data
    /// \param nLength Number of bytes received
    void ProcessOut(size_t nLength);

    // ========================================================================
    // SCSI Command Processing
    // ========================================================================

    /// \brief Main SCSI command handler - dispatches to specific command handlers
    void HandleSCSICommand();

    /// \brief Send Command Status Wrapper (CSW) to complete command
    void SendCSW();

    // Sense data management for SCSI error reporting
    void setSenseData(u8 senseKey, u8 asc = 0, u8 ascq = 0);
    void clearSenseData();
    void sendCheckCondition();
    void sendGoodStatus();

    // ========================================================================
    // CD-ROM Specific Command Handlers (BlueSCSI-inspired)
    // ========================================================================

    // TOC and session management
    void DoReadTOC(bool msf, uint8_t startingTrack, uint16_t allocationLength);
    void DoReadSessionInfo(bool msf, uint16_t allocationLength);
    void DoReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD);
    void DoReadHeader(bool MSF, uint32_t lba, uint16_t allocationLength);
    void DoReadTrackInformation(u8 addressType, u32 address, u16 allocationLength);

    // TOC formatting helpers
    void FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF);
    void FormatRawTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool useBCD);

    // ========================================================================
    // CUE Sheet and Track Management
    // ========================================================================

    /// \brief Get track information for a specific LBA
    CUETrackInfo GetTrackInfoForLBA(u32 lba);

    /// \brief Get track information by track number
    CUETrackInfo GetTrackInfoForTrack(int track);

    /// \brief Get last track number on disc
    int GetLastTrackNumber();

    /// \brief Calculate lead-out LBA (end of disc)
    u32 GetLeadoutLBA();

    // Track format helpers
    int GetBlocksize();
    int GetBlocksizeForTrack(CUETrackInfo trackInfo);
    int GetSkipbytes();
    int GetSkipbytesForTrack(CUETrackInfo trackInfo);
    int GetMediumType();
    int GetSectorLengthFromMCS(uint8_t mainChannelSelection);
    int GetSkipBytesFromMCS(uint8_t mainChannelSelection);

    // ========================================================================
    // Address Conversion Utilities (BlueSCSI-inspired)
    // ========================================================================

    /// \brief Convert LBA to MSF format (binary)
    void LBA2MSF(int32_t LBA, uint8_t *MSF, bool relative);

    /// \brief Convert LBA to MSF format (BCD)
    void LBA2MSFBCD(int32_t LBA, uint8_t *MSF, bool relative);

    /// \brief Convert MSF to LBA
    int32_t MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative);

    /// \brief Get address in requested format (LBA or MSF)
    u32 GetAddress(u32 lba, int msf, boolean relative);

    /// \brief Convert LBA to packed MSF value
    u32 lba_to_msf(u32 lba, boolean relative = false);

    /// \brief Convert MSF to LBA
    u32 msf_to_lba(u8 minutes, u8 seconds, u8 frames);

    // ========================================================================
    // Device Initialization
    // ========================================================================

    /// \brief Initialize device size and capacity
    void InitDeviceSize(u64 blocks);

    /// \brief Convert ASCII string to UTF-16 USB string descriptor
    const void *ToStringDescriptor(const char *pString, size_t *pLength);

    // ========================================================================
    // State Machine
    // ========================================================================

    /// \brief SCSI command processing states
    enum TCDState
    {
        Init,              // Initial state
        ReceiveCBW,        // Waiting for Command Block Wrapper
        InvalidCBW,        // Invalid CBW received
        DataIn,            // Sending data to host
        DataOut,           // Receiving data from host
        SentCSW,           // CSW sent, waiting for confirmation
        SendReqSenseReply, // Sending Request Sense reply
        DataInRead,        // Reading data from device for transfer
        DataOutWrite       // Writing received data to device
    };

    /// \brief Media state for proper macOS Unit Attention handling
    enum class MediaState
    {
        NO_MEDIUM,                     // No disc present
        MEDIUM_PRESENT_UNIT_ATTENTION, // Disc present but needs Unit Attention
        MEDIUM_PRESENT_READY           // Disc present and ready
    };

    /// \brief USB endpoint numbers
    enum TEPNumber
    {
        EPIn = 1,  // Bulk IN endpoint
        EPOut = 2, // Bulk OUT endpoint
        NumEPs     // Total number of endpoints
    };

    // ========================================================================
    // Static Configuration Data
    // ========================================================================

    static const TUSBDeviceDescriptor s_DeviceDescriptor;
    static const char *const s_StringDescriptorTemplate[];
    static const char *const s_StringDescriptor[];

    /// \brief USB configuration descriptor with interface and endpoints
    struct TUSBMSTGadgetConfigurationDescriptor
    {
        TUSBConfigurationDescriptor Configuration;
        TUSBInterfaceDescriptor Interface;
        TUSBEndpointDescriptor EndpointIn;
        TUSBEndpointDescriptor EndpointOut;
    } PACKED;

    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorFullSpeed;
    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorHighSpeed;

    // ========================================================================
    // Instance Variables - Device and USB State
    // ========================================================================

    ICueDevice *m_pDevice;               // CUE-aware block device
    CUSBCDGadgetEndpoint *m_pEP[NumEPs]; // Endpoint objects

    TCDState m_nState;       // SCSI command state machine
    MediaState m_mediaState; // Media presence state
    MEDIA_TYPE m_mediaType;  // CD or DVD

    boolean m_CDReady;     // Device ready flag
    boolean m_IsFullSpeed; // USB 1.1 full-speed vs USB 2.0 high-speed
    boolean discChanged;   // Media change flag
    bool m_bDebugLogging;  // Enable verbose logging

    // ========================================================================
    // Instance Variables - USB Protocol Buffers
    // ========================================================================

    alignas(4) TUSBCDCBW m_CBW; // Command Block Wrapper
    alignas(4) TUSBCDCSW m_CSW; // Command Status Wrapper

    // Buffer size constants
    static const size_t MaxOutMessageSize = 2048;
    static const size_t MaxBlocksToRead = 16;
    static const size_t MaxSectorSize = 2352;
    static const size_t MaxInMessageSize = MaxBlocksToRead * MaxSectorSize;

    DMA_BUFFER(u8, m_InBuffer, MaxInMessageSize);   // DMA buffer for IN transfers
    DMA_BUFFER(u8, m_OutBuffer, MaxOutMessageSize); // DMA buffer for OUT transfers
    u8 *m_FileChunk;                                // Temporary buffer for file reads

    // ========================================================================
    // Instance Variables - SCSI Reply Structures
    // ========================================================================

    TUSBCDInquiryReply m_InqReply{
        0x05,                                     // Peripheral type = CD/DVD
        0x80,                                     // RMB set = removable media
        0x00,                                     // Version 0x00 = no standard (3 = SPC, 4 = SPC2, 5 = SPC3)
        0x32,                                     // Response Data Format = This response is SPC3 format
        0x1F,                                     // Additional Length
        0x50,                                     // SCCS ACC TPGS 3PC Reserved PROTECT
        0x00,                                     // BQUE ENCSERV VS MULTIP MCHNGR Obsolete Obsolete ADDR16a
        0x00,                                     // Obsolete Obsolete WBUS16a SYNCa LINKED Obsolete CMDQUE VS
        {'U', 'S', 'B', 'O', 'D', 'E', ' ', ' '}, // Vendor Identification
        {'C', 'D', 'R', 'O', 'M', ' ', 'E', 'M', 'U', 'L', 'A', 'T', 'O', 'R', ' ', ' '},
        {'0', '0', '0', '1'}, // Product Revision
        {0},                  // Vendor specific (20 bytes, all zeros)
        {0},                  // Reserved (2 bytes)
        {0},                  // Version descriptors (16 bytes, all zeros for now)
        {0}                   // Reserved/padding (22 bytes)
    }; // INQUIRY response
    TUSBUintSerialNumberPage m_InqSerialReply{0x80, 0x00, 0x0000, 0x04, {'0', '0', '0', '0'}}; // Unit serial number VPD page
    TUSBSupportedVPDPage m_InqVPDReply{0x00, 0x00, 0x0000, 0x01, 0x80};                        // Supported VPD pages
    TUSBCDModeSenseReply m_ModeSenseReply{3, 0, 0, 0};                                         // MODE SENSE response
    TUSBCDReadCapacityReply m_ReadCapReply{
        htonl(0x00),  // get's overridden in CUSBCDGadget::InitDeviceSize
        htonl(2048)}; // READ CAPACITY response
    TUSBCDRequestSenseReply m_ReqSenseReply{
        0x70, // current error
        0x00, // reserved
        0x00, // Sense Key
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
         0x0, 0x0}, // information
        0x10,       // additional sense length
        {0x0, 0x0,
         0x0, 0x0},     // command specific information
        0x00,           // additional sense code qualifier https://www.t10.org/lists/asc-num.htm
        0x00,           // additional sense code qualifier https://www.t10.org/lists/asc-num.htm
        0x00,           // field replacement unit code
        0x00,           // sksv
        {0x0, 0x0, 0x0} // sense key specific
    }; // REQUEST SENSE response
    TUSBCDReadTOCReply m_TOCReply{
        htons(0x000A), // TOC data length (10 bytes follow)
        0x01,          // First track number
        0x01,          // Last track number
        0x00,          // Reserved
        0x14,          // ADR = 1 (LBA), Control = 4 (Data track)
        0x01,          // Track number
        0x00,          // Reserved
        htonl(0x00)    // Track start LBA (big-endian 0)
    }; // READ TOC response (basic format)
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
        {0x00}}; // READ DISC INFORMATION response

    // GET CONFIGURATION feature descriptors
    TUSBCDFeatureHeaderReply header{
        htons(0x0000),       // datalength
        0x00,                // reserved;
        htons(PROFILE_CDROM) // currentProfile;
    };
    TUSBCDProfileListFeatureReply profile_list{
        htons(0x0000), // featureCode
        0x03,          // VersionPersistentCurrent
        0x04           // AdditionalLength
    };
    TUSBCProfileDescriptorReply cdrom_profile{
        htons(PROFILE_CDROM), // profileNumber
        0x01,                 // currentP
        0x00                  // reserved
    };
    TUSBCProfileDescriptorReply dvd_profile{
        htons(PROFILE_DVD_ROM), // profileNumber
        0x01,                   // currentP
        0x00                    // reserved
    };
    TUSBCDCoreFeatureReply core{
        htons(0x0001), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x08,          // AdditionalLength
        0x08,          // physicalInterfaceStandard
        0x03,          // INQ2DBE
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    TUSBCDMorphingFeatureReply morphing{
        htons(0x0002), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x02,          // OCEventASYNC
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    TUSBCDRemovableMediumFeatureReply mechanism{
        htons(0x0003), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x15,          // Mechanism
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    TUSBCDAnalogueAudioPlayFeatureReply audioplay{
        htons(0x0103), // featureCode;
        0x0b,          // VersionPersistentCurrent;
        0x04,          // AdditionalLength;
        0x00,          // ScanSCMSV;
        0x00,          // reserved1;
        0xff           // NumVolumeLevels;
    };
    TUSBCDMultiReadFeatureReply multiread{
        htons(0x001d), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x00,          // AdditionalLength
    };
    TUSBCDPowerManagementFeatureReply powermanagement{
        htons(0x0100), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x00,          // AdditionalLength
    };
    TUSBCDCDReadFeatureReply cdread{
        htons(0x001e), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x00,          // DAPC2FlagsCDText
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    TUSBCDDVDReadFeatureReply dvdread{
        htons(0x001f), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x01,          // MultiUnitsDUALLayerBuff (MULTI110=0, DUAL_L=0, BUFF=1)
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };

    // ========================================================================
    // Instance Variables - Transfer State
    // ========================================================================

    u32 m_nblock_address;  // Current LBA for transfer
    u32 m_nnumber_blocks;  // Remaining blocks to transfer
    u32 m_nbyteCount;      // Remaining bytes to transfer
    u32 m_nLastReadEndLBA; // Last read position (for consecutive read optimization)

    u8 bmCSWStatus;                // CSW status code
    SenseParameters m_SenseParams; // Current sense data

    // Sector format parameters
    int data_skip_bytes;     // Skip bytes for data track reads
    int data_block_size;     // Data block size
    int skip_bytes;          // Skip bytes for current operation
    int block_size;          // Physical block size on disc
    int transfer_block_size; // Block size for USB transfer
    int file_mode;           // File/track mode
    int numTracks;           // Number of tracks on disc
    uint8_t mcs;             // Main channel selection bits

    // ========================================================================
    // Instance Variables - CUE Parsing and Device Identification
    // ========================================================================

    CUEParser cueParser; // CUE sheet parser

    char m_HardwareSerialNumber[20];   // Hardware serial number (e.g., "USBODE-XXXXXXXX")
    const char *m_StringDescriptor[4]; // USB string descriptors
    u8 m_StringDescriptorBuffer[80];   // Buffer for string descriptor conversion
};

#endif