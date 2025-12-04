//
// scsidefs.h
//
// SCSI Definitions and Structures
//
#ifndef _circle_usb_gadget_scsidefs_h
#define _circle_usb_gadget_scsidefs_h

#include <circle/types.h>
#include <circle/macros.h>

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

// reply to SCSI Mode Sense(6) 0x1A
struct ModeSense6Header
{
    u8 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u8 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE6_HEADER 4

struct TUSBCDModeSenseReply // 4 bytes
{
    u8 bModeDataLen;
    u8 bMedType;
    u8 bDevParam;
    u8 bBlockDecrLen;
} PACKED;
#define SIZE_MODEREP 4

// SCSI Mode Sense(10) Response Structures
struct ModeSense10Header
{
    u16 modeDataLength;
    u8 mediumType;
    u8 deviceSpecificParameter;
    u16 reserved; // Reserved (was incorrectly u32 blockDescriptorLength)
    u16 blockDescriptorLength;
} PACKED;
#define SIZE_MODE_SENSE10_HEADER 8

// Mode Page 0x01 (Read/Write Error Recovery Parameters Mode Page)
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

// Mode Page 0x08 (Caching)
struct ModePage0x08Data
{
    u8 pageCodeAndPS;     // 0x08
    u8 pageLength;        // 0x12 (18 bytes)
    u8 cachingFlags;      // Bit 2: WCE, Bit 0: RCD
    u8 demandReadRetention;
    u8 writeRetention;
    u16 disablePrefetchTransfer;
    u16 minPrefetch;
    u16 maxPrefetch;
    u16 maxPrefetchCeiling;
    u8 flags2;
    u8 numCacheSegments;
    u16 cacheSegmentSize;
    u8 reserved;
    u8 obsolete[3];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X08 20
// Mode Page 0x0E (CD Audio Control Page)
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
// Mode Page 0x1C (Informational Exceptions Control)
struct ModePage0x1CData
{
    u8 pageCodeAndPS;     // 0x1C
    u8 pageLength;        // 0x0A (10 bytes)
    u8 flags;             // PERF, EBF, EWASC, DEXCPT, TEST, LOGERR
    u8 mrie;              // Method of Reporting Informational Exceptions
    u32 intervalTimer;    // Interval timer (big-endian)
    u32 reportCount;      // Report count (big-endian)
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X1C 12
// Mode Page 0x1A (Power Condition)
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

// Mode Page 0x2A (MM Capabilities and Mechanical Status) Data
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

struct ModePage0x30Data
{
    u8 pageCodeAndPS;      // 0x30
    u8 pageLength;         // 0x16 (22 bytes)
    u8 appleID[20];        // "APPLE COMPUTER, INC   " (Padded with spaces)
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X30 22

struct ModePage0x31Data
{
    uint8_t pageCodeAndPS;
    uint8_t pageLength;
    uint8_t appleID[20];
} PACKED;
#define SIZE_MODE_SENSE10_PAGE_0X31 22

// reply to SCSI Read Capacity 0x25
struct TUSBCDReadCapacityReply // 8 bytes
{
    u32 nLastBlockAddr; // Last logical block address
    u32 nSectorSize;    // Block size in bytes
} PACKED;
#define SIZE_READCAPREP 8

// READ TOC (0x43) - Format 0 response
struct TUSBCDReadTOCReply // 12 bytes
{
    u16 length; // TOC data length (excluding this field)
    u8 firstTrack;
    u8 lastTrack;
    u8 reserved;
    u8 adr_ctrl;    // 0x14 = ADR=1 (LBA), Control=4 (Data track)
    u8 trackNumber; // 1 = Track 1
    u8 reserved2;
    u32 trackStartLBA; // LBA start of track (e.g. 0)
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
    u16 DataLength; // Total length of the TOC data (excluding the length itself)
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
    u8 dataFormatCode; // this should be 0x01
    u8 adrControl;     // 0x00 = Q Sub-channel mode information not supplied / 2 audio channels without pre-emphasis
    u8 trackNumber;
    u8 indexNumber;
    u32 absoluteAddress;
    u32 relativeAddress;
} PACKED;
#define SIZE_SUBCHANNEL_01_DATA_REPLY 12

// READ HEADER (0x44)
struct TUSBCDReadDiscStructureHeader
{
    u16 dataLength;
    u8 reserved[2];
} PACKED;

// DVD Physical Format Information (Format 0x00) - 17 bytes payload
struct DVDPhysicalFormatInfo
{
    // Byte 0: Book type and part version
    u8 bookTypePartVer;      // bits 7-4: book type, bits 3-0: part version
    // Byte 1: Disc size and maximum transfer rate
    u8 discSizeMaxRate;      // bits 7-4: max rate, bits 3-0: disc size
    // Byte 2: Number of layers, track path, layer type
    u8 layersPathType;       // bit 7: reserved, bits 6-5: num layers, bit 4: track path, bits 3-0: layer type
    // Byte 3: Linear density and track density
    u8 densities;            // bits 7-4: track density, bits 3-0: linear density
    // Bytes 4-6: Data area start sector (24-bit, big-endian)
    u8 dataStartSector[3];   // Start sector of data area
    // Bytes 7-9: Data area end sector (24-bit, big-endian)
    u8 dataEndSector[3];     // End sector of data area
    // Bytes 10-12: End sector of layer 0 (24-bit, big-endian)
    u8 layer0EndSector[3];   // End sector of layer 0 (for dual-layer)
    // Byte 13: BCA flag
    u8 bcaFlag;              // bit 7: BCA present, bits 6-0: reserved
    // Bytes 14-16: Media specific (reserved for DVD-ROM)
    u8 reserved[3];
} PACKED;
#define SIZE_DVD_PHYSICAL_FORMAT 17
// DVD Copyright Information (Format 0x01) - 4 bytes payload
struct DVDCopyrightInfo
{
    u8 copyrightProtectionType;  // 0x00=None, 0x01=CSS/CPPM
    u8 regionManagementInfo;     // Region codes (bitfield)
    u8 reserved1;
    u8 reserved2;
} PACKED;
#define SIZE_DVD_COPYRIGHT_INFO 4

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
    u16 data_length;             // Bytes 0–1: Length of remaining data (not including this field), e.g. 0x0020
    u8 disc_status;              // Byte 2: Disc status & erasable flags
    u8 first_track_number;       // Byte 3: First Track Number
    u8 number_of_sessions;       // Byte 4: Number of Sessions
    u8 first_track_last_session; // Byte 5: First Track Number in Last Session
    u8 last_track_last_session;  // Byte 6: Last Track Number in Last Session
    u8 reserved1;                // Byte 7: Reserved
    u8 disc_type;                // Byte 8: Disc Type (e.g. 0 = CD-ROM)
    u8 reserved2;                // Byte 9: Reserved
    u32 disc_id;                 // Bytes 10–13: Disc Identification (optional, usually zero)
    u32 last_lead_in_start_time; // Bytes 14–17: Start time of last session's lead-in (optional)
    u32 last_possible_lead_out;  // Bytes 18–21: Last possible lead-out start time
    u8 disc_bar_code[8];         // Bytes 22–29: Disc Bar Code (optional)
    u32 reserved3;               // Bytes 30–33: Reserved / padding
} PACKED;
#define SIZE_DISC_INFO_REPLY 34
// GET EVENT STATUS NOTIFICATION (0x4A)
struct TUSBCDEventStatusReplyHeader
{
    u16 eventDataLength;    // 2 bytes: length of remaining data
    u8 notificationClass;   // Media class
    u8 supportedEventClass; // No events supported
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

// Feature 0010h - Random Readable - Ability to read data from random locations
struct TUSBCDRandomReadableFeatureReply
{
    u16 featureCode;           // 0x0010
    u8 VersionPersistentCurrent; // Version, Persistent, Current bits
    u8 AdditionalLength;       // Length of additional data (8 bytes)
    u32 blockSize;             // Logical block size (2048 bytes, big-endian)
    u16 blocking;              // Number of logical blocks per device read (big-endian)
    u8 pp;                     // Error Recovery Page Present bit
    u8 reserved;               // Reserved
} PACKED;
#define SIZE_RANDOM_READABLE_REPLY 12
// Feature 0106h - DVD CSS - Content Scramble System support
struct TUSBCDDVDCSSFeatureReply
{
    u16 featureCode;           // 0x0106
    u8 VersionPersistentCurrent; // Version, Persistent, Current bits
    u8 AdditionalLength;       // Length of additional data (4 bytes)
    u8 reserved1;              // Reserved
    u8 reserved2;              // Reserved
    u8 reserved3;              // Reserved
    u8 cssVersion;             // CSS version number (0x01)
} PACKED;
#define SIZE_DVD_CSS_REPLY 8

// Feature 0107h - Real Time Streaming - Support for real-time data streaming
struct TUSBCDRealTimeStreamingFeatureReply
{
    u16 featureCode;           // 0x0107
    u8 VersionPersistentCurrent; // Version, Persistent, Current bits
    u8 AdditionalLength;       // Length of additional data (4 bytes)
    u8 flags;                  // SW, WSPD, MP2A, SCS, RBCB bits
    u8 reserved1;              // Reserved
    u8 reserved2;              // Reserved
    u8 reserved3;              // Reserved
} PACKED;
#define SIZE_REAL_TIME_STREAMING_REPLY 8

struct TUSBCDAudioConfigurationDescriptor
{
    TUSBConfigurationDescriptor Configuration;

    // Data interface (existing bulk endpoints)
    TUSBInterfaceDescriptor DataInterface;
    TUSBEndpointDescriptor EndpointInBulk;
    TUSBEndpointDescriptor EndpointOutBulk;

    // Audio streaming interface (alternate setting 0 - no bandwidth)
    TUSBInterfaceDescriptor AudioInterfaceAlt0;

    // Audio streaming interface (alternate setting 1 - active)
    TUSBInterfaceDescriptor AudioInterfaceAlt1;
    TUSBEndpointDescriptor EndpointInAudio;
} PACKED;

// ============================================================================
// Vendor-Specific Toolbox Commands
// ============================================================================

struct TUSBCDToolboxFileEntry
{
    u8 index;    /* byte 00: file index in directory */
    u8 type;     /* byte 01: type 0 = file, 1 = directory */
    u8 name[33]; /* byte 02-34: filename (32 byte max) + space for NUL terminator */
    u8 size[5];  /* byte 35-39: file size (40 bit big endian unsigned) */
} PACKED;

#endif
