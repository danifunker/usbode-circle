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
#include "usb_structs.h"
#include "scsi_structs.h"
#include "cdrom_util.h"

#ifndef USB_GADGET_DEVICE_ID_CD
#define USB_GADGET_DEVICE_ID_CD 0x1d6b
#endif

#define htons(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)

// ============================================================================
// Main USB CD-ROM Gadget Class
// ============================================================================

class ScsiCommandDispatcher;

class CUSBCDGadget : public CDWUSBGadget
{
public:
	friend class ScsiCommandDispatcher;
	friend CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* pGadget, u32 lba);
	friend CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* pGadget, int track);
	friend int GetLastTrackNumber(CUSBCDGadget* pGadget);
	friend u32 GetLeadoutLBA(CUSBCDGadget* pGadget);
	friend int GetBlocksize(CUSBCDGadget* pGadget);
	friend int GetSkipbytes(CUSBCDGadget* pGadget);
	friend int GetMediumType(CUSBCDGadget* pGadget);
    /// \param pInterruptSystem Pointer to the interrupt system object
    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \note pDevice must be initialized yet, when it is specified here.
    /// \note SetDevice() has to be called later, when pDevice is not specified here.
    CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed, ICueDevice *pDevice = nullptr);

    ~CUSBCDGadget(void);

    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \note Call this, if pDevice has not been specified in the constructor.
    void SetDevice(ICueDevice *pDevice);

    /// \brief Call this periodically from TASK_LEVEL to allow I/O operations!
    void Update(void);

    /// \param nBlocks Capacity of the block device in number of blocks (a 512 bytes)
    /// \note Used when the block device does not report its size.
    // void SetDeviceBlocks(u64 nBlocks);
    /// \return Capacity of the block device in number of blocks (a 512 bytes)
    // u64 GetBlocks (void) const;

protected:
    // ========================================================================
    // CDWUSBGadget Overrides
    // ========================================================================

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

private:
    void AddEndpoints(void) override;
    void CreateDevice(void) override;
    void OnSuspend(void) override;
    int OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData) override;
    // ========================================================================
    // USB Transfer Callbacks (called from IRQ level via CUSBCDGadgetEndpoint)
    // ========================================================================
    friend class CUSBCDGadgetEndpoint;
    void OnTransferComplete(boolean bIn, size_t nLength);
    void OnActivate(); // called from OUT ep
    void ProcessOut(size_t nLength);
    // ========================================================================
    // SCSI Command Processing
    // ========================================================================
    void HandleSCSICommand();
    void SendCSW();
    // Sense data management helpers for MacOS compatibility
    void setSenseData(u8 senseKey, u8 asc = 0, u8 ascq = 0);
    void clearSenseData();
    void sendCheckCondition();
    void sendGoodStatus();

	// SCSI Command Data Handlers (definitions are in scsi_command_dispatcher.cpp)
    void FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF);
    void FormatRawTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool useBCD);
    void DoReadTOC(bool msf, uint8_t startingTrack, uint16_t allocationLength);
    void DoReadSessionInfo(bool msf, uint16_t allocationLength);
    void DoReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD);
	void DoReadHeader(bool MSF, uint32_t lba, uint16_t allocationLength);
    void DoReadTrackInformation(u8 addressType, u32 address, u16 allocationLength);


    // ========================================================================
    // CUE Sheet and Track Management
    // ========================================================================

    const char *m_StringDescriptor[4];
    int GetSectorLengthFromMCS(uint8_t mainChannelSelection);
    int GetSkipBytesFromMCS(uint8_t mainChannelSelection);


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

    // Media state for proper MacOS Unit Attention handling
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

    /// \brief USB configuration descriptor with interface and endpoints
    struct TUSBMSTGadgetConfigurationDescriptor
    {
        TUSBConfigurationDescriptor Configuration;
        TUSBInterfaceDescriptor Interface;
        TUSBEndpointDescriptor EndpointIn;
        TUSBEndpointDescriptor EndpointOut;
    } PACKED;

    struct TUSBMSTGadgetConfigurationDescriptorHighSpeedWithAudio
{
    TUSBConfigurationDescriptor Configuration;
    
    // Data interface (bulk endpoints)
    TUSBInterfaceDescriptor DataInterface;
    TUSBEndpointDescriptor EndpointInBulk;
    TUSBEndpointDescriptor EndpointOutBulk;
    
    // Audio streaming interface - Alternate 0 (zero bandwidth)
    TUSBInterfaceDescriptor AudioInterfaceAlt0;
    
    // Audio streaming interface - Alternate 1 (active)
    TUSBInterfaceDescriptor AudioInterfaceAlt1;
    TUSBEndpointDescriptor EndpointInAudio;
} PACKED;

    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorFullSpeed;
    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorHighSpeed;

    // ========================================================================
    // Instance Variables - Device and USB State
    // ========================================================================

    ICueDevice *m_pDevice;               // CUE-aware block device
    CUSBCDGadgetEndpoint *m_pEP[NumEPs]; // Endpoint objects

    TCDState m_nState = Init; // SCSI command state machine
    MediaState m_mediaState = MediaState::NO_MEDIUM;
    MEDIA_TYPE m_mediaType = MEDIA_TYPE::CD;

    boolean m_CDReady = false;   // Device ready flag
    boolean m_IsFullSpeed = 0;   // USB 1.1 full-speed vs USB 2.0 high-speed
    boolean discChanged = false; // Media change flag
    bool m_bDebugLogging;        // Debug flag to enable verbose CD-ROM logging
    // ========================================================================
    // Instance Variables - USB Protocol Buffers
    // ========================================================================

    alignas(4) TUSBCDCBW m_CBW; // Command Block Wrapper
    alignas(4) TUSBCDCSW m_CSW; // Command Status Wrapper

    // Buffer size constants
    static const size_t MaxOutMessageSize = 2048;
    static const size_t MaxBlocksToReadFullSpeed = 16;  // USB 1.1: 16 blocks = 37,632 bytes max
    static const size_t MaxBlocksToReadHighSpeed = 32;  // USB 2.0: 32 blocks = 75,264 bytes max
    static const size_t MaxSectorSize = 2352;
    static const size_t MaxInMessageSize = MaxBlocksToReadHighSpeed * MaxSectorSize; // 75,264 bytes
    static const size_t MaxInMessageSizeFullSpeed = MaxBlocksToReadFullSpeed * MaxSectorSize; // 37,632 bytes

    alignas(64) DMA_BUFFER(u8, m_InBuffer, MaxInMessageSize);       // USB IN transfers
    alignas(64) DMA_BUFFER(u8, m_OutBuffer, MaxOutMessageSize);     // USB OUT transfers  
    alignas(64) DMA_BUFFER(u8, m_FileChunk, MaxInMessageSize);      // File staging buffer    
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
    };
    TUSBUintSerialNumberPage m_InqSerialReply{0x80, 0x00, 0x0000, 0x04, {'0', '0', '0', '0'}};

    TUSBSupportedVPDPage m_InqVPDReply{0x00, 0x00, 0x0000, 0x01, 0x80};

    TUSBCDModeSenseReply m_ModeSenseReply{3, 0, 0, 0};
    TUSBCDReadCapacityReply m_ReadCapReply{
        htonl(0x00), // get's overridden in CUSBCDGadget::InitDeviceSize
        htonl(2048)};

    TUSBCDRequestSenseReply m_ReqSenseReply = {
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
    };

    // static for now
    TUSBCDReadTOCReply m_TOCReply = {
        htons(0x000A), // TOC data length (10 bytes follow)
        0x01,          // First track number
        0x01,          // Last track number
        0x00,          // Reserved
        0x14,          // ADR = 1 (LBA), Control = 4 (Data track)
        0x01,          // Track number
        0x00,          // Reserved
        htonl(0x00)    // Track start LBA (big-endian 0)
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

    // GET CONFIGURATION feature descriptors
    TUSBCDFeatureHeaderReply header = {
        htons(0x0000),       // datalength
        0x00,                // reserved;
        htons(PROFILE_CDROM) // currentProfile;
    };
    // Feature 0000h - Profile List - A list of all profile supported by the drive
    TUSBCDProfileListFeatureReply profile_list = {
        htons(0x0000), // featureCode
        0x03,          // VersionPersistentCurrent
        0x04           // AdditionalLength
    };
    // Profiles 0008h CD-ROM
    TUSBCProfileDescriptorReply cdrom_profile = {
        htons(PROFILE_CDROM), // profileNumber
        0x01,                 // currentP
        0x00                  // reserved
    };
    // Profiles 0010h DVD-ROM
    TUSBCProfileDescriptorReply dvd_profile = {
        htons(PROFILE_DVD_ROM), // profileNumber
        0x01,                   // currentP
        0x00                    // reserved
    };
    // Feature 0001h - Core
    TUSBCDCoreFeatureReply core = {
        htons(0x0001), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x08,          // AdditionalLength
        0x08,          // physicalInterfaceStandard
        0x03,          // INQ2DBE
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    // Feature 0002h - Morphing Feature. The Drive is able to report operational changes
    TUSBCDMorphingFeatureReply morphing = {
        htons(0x0002), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x02,          // OCEventASYNC
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    // Feature 0003h - Removable Medium. The medium may be removed from the device
    TUSBCDRemovableMediumFeatureReply mechanism = {
        htons(0x0003), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x15,          // Mechanism
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    // Feture 0103h - CD Audio External Play Feature
    TUSBCDAnalogueAudioPlayFeatureReply audioplay = {
        htons(0x0103), // featureCode;
        0x0b,          // VersionPersistentCurrent;
        0x04,          // AdditionalLength;
        0x00,          // ScanSCMSV;
        0x00,          // reserved1;
        0xff           // NumVolumeLevels;
    };
    // Feature 001dh - Multi-Read - The ability to read all CD media types
    TUSBCDMultiReadFeatureReply multiread = {
        htons(0x001d), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x00,          // AdditionalLength
    };
    // Feature 0100h - Power Management Feature
    TUSBCDPowerManagementFeatureReply powermanagement = {
        htons(0x0100), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x00,          // AdditionalLength
    };
    // Feature 001eh - CD Read - The ability to read CD specific structures
    TUSBCDCDReadFeatureReply cdread = {
        htons(0x001e), // featureCode
        0x0b,          // VersionPersistentCurrent
        0x04,          // AdditionalLength
        0x00,          // DAPC2FlagsCDText
        0x00,          // reserved
        0x00,          // reserved
        0x00           // reserved
    };
    // Feature 001fh - DVD Read - The ability to read DVD specific structures
    TUSBCDDVDReadFeatureReply dvdread = {
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

    u32 m_nblock_address;
    u32 m_nnumber_blocks;
    u32 m_nbyteCount;

    u8 bmCSWStatus = 0;
    SenseParameters m_SenseParams; // Current sense data

    // Sector format parameters
    int data_skip_bytes = 0;        // Skip bytes for data track reads
    int data_block_size = 2048;     // Data block size
    int skip_bytes = 0;             // Skip bytes for current operation
    int block_size = 2048;          // Physical block size on disc
    int transfer_block_size = 2048; // Block size for USB transfer
    int file_mode = 1;              // File/track mode
    int numTracks = 0;              // Number of tracks on disc
    uint8_t mcs = 0;

    // ========================================================================
    // Instance Variables - CUE Parsing and Device Identification
    // ========================================================================

    CUEParser cueParser;

    char m_HardwareSerialNumber[20];               // Hardware serial number (e.g., "USBODE-XXXXXXXX")
    static const char *const s_StringDescriptor[]; // USB string descriptors
    u8 m_StringDescriptorBuffer[80];               // Buffer for string descriptor conversion
};

#endif
