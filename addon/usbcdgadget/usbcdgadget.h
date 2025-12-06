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
#include <discimage/imagedevice.h>
#include <usbcdgadget/scsidefs.h>

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
// Main USB CD-ROM Gadget Class
// ============================================================================

class CUSBCDGadget : public CDWUSBGadget
{
public:
    /// \param pInterruptSystem Pointer to the interrupt system object
    /// \param isFullSpeed True for USB 1.1 Full Speed, false for USB 2.0 High Speed
    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \param usVendorId USB Vendor ID (defaults to USB_GADGET_VENDOR_ID)
    /// \param usProductId USB Product ID (defaults to USB_GADGET_DEVICE_ID_CD)
    /// \note pDevice must be initialized yet, when it is specified here.
    /// \note SetDevice() has to be called later, when pDevice is not specified here.
    CUSBCDGadget(CInterruptSystem *pInterruptSystem, boolean isFullSpeed,
                 IImageDevice *pDevice = nullptr,
                 u16 usVendorId = USB_GADGET_VENDOR_ID,
                 u16 usProductId = USB_GADGET_DEVICE_ID_CD);

    ~CUSBCDGadget(void);

    /// \param pDevice Pointer to the block device, to be controlled by this gadget
    /// \note Call this, if pDevice has not been specified in the constructor.
    void SetDevice(IImageDevice *pDevice);

    /// \brief Call this periodically from TASK_LEVEL to allow I/O operations!
    void Update(void);

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

    // Command handler function pointer type
    typedef void (*SCSIHandler)(CUSBCDGadget *gadget);

    // Function table for SCSI handlers
    SCSIHandler m_SCSIHandlers[256];

    // Helper to initialize the handler table
    void InitSCSIHandlers();

    void HandleSCSICommand();
    void SendCSW();

    // Sense data management helpers for MacOS compatibility
    void setSenseData(u8 senseKey, u8 asc = 0, u8 ascq = 0);
    void clearSenseData();
    void sendCheckCondition();
    void sendGoodStatus();
    char m_USBTargetOS[16];

    // Friend declarations for command classes and utilities
    friend class SCSIInquiry;
    friend class SCSIRead;
    friend class SCSITOC;
    friend class SCSIToolbox;
    friend class SCSIMisc;
    friend class CDUtils;

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

    static TUSBDeviceDescriptor s_DeviceDescriptor;
    static TUSBDeviceDescriptor s_DeviceDescriptorMacOS9;
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
    static const TUSBMSTGadgetConfigurationDescriptor s_ConfigurationDescriptorMacOS9;
    // ========================================================================
    // Instance Variables - Device and USB State
    // ========================================================================

    IImageDevice *m_pDevice;             // Image device (Plugin System)
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
    static const size_t MaxBlocksToReadFullSpeed = 16; // USB 1.1: 16 blocks = 37,632 bytes max
    static const size_t MaxBlocksToReadHighSpeed = 32; // USB 2.0: 32 blocks = 75,264 bytes max
    static const size_t MaxSectorSize = 2352;
    static const size_t MaxInMessageSize = MaxBlocksToReadHighSpeed * MaxSectorSize;          // 75,264 bytes
    static const size_t MaxInMessageSizeFullSpeed = MaxBlocksToReadFullSpeed * MaxSectorSize; // 37,632 bytes

    alignas(64) DMA_BUFFER(u8, m_InBuffer, MaxInMessageSize);   // USB IN transfers
    alignas(64) DMA_BUFFER(u8, m_OutBuffer, MaxOutMessageSize); // USB OUT transfers
    alignas(64) DMA_BUFFER(u8, m_FileChunk, MaxInMessageSize);  // File staging buffer
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
        0x03,          // DAPC2FlagsCDText
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

    // Feature 0010h - Random Readable - Ability to read data from random locations
    // Block size: 2048 bytes, Blocking: 1, PP (Error Recovery Page Present): 1
    TUSBCDRandomReadableFeatureReply randomreadable = {
        htons(0x0010), // featureCode
        0x01,          // VersionPersistentCurrent (version=0, persistent=0, current=1)
        0x08,          // AdditionalLength (8 bytes)
        htonl(2048),   // blockSize (2048 bytes for CD-ROM)
        htons(1),      // blocking (1 logical block per read)
        0x01,          // pp (Error Recovery Page Present)
        0x00           // reserved
    };

    // Feature 0107h - Real Time Streaming - Essential for smooth CD-DA playback
    // This feature tells the OS that the device can maintain real-time audio streams
    // Flags: SW=1, WSPD=1, MP2A=1, SCS=1 (bits 0-3)
    TUSBCDRealTimeStreamingFeatureReply rtstreaming = {
        htons(0x0107), // featureCode
        0x01,          // VersionPersistentCurrent (version=0, persistent=0, current=1)
        0x04,          // AdditionalLength (4 bytes)
        0x0F,          // flags (SW=1, WSPD=1, MP2A=1, SCS=1, RBCB=0)
        0x00,          // reserved1
        0x00,          // reserved2
        0x00           // reserved3
    };

    // Feature 0106h - DVD CSS - Content Scramble System support
    // This feature indicates CSS copy protection support for DVDs
    // CSS Version 1 is the standard version
    TUSBCDDVDCSSFeatureReply dvdcss = {
        htons(0x0106), // featureCode
        0x01,          // VersionPersistentCurrent (version=0, persistent=0, current=1)
        0x04,          // AdditionalLength (4 bytes)
        0x00,          // reserved1
        0x00,          // reserved2
        0x00,          // reserved3
        0x01           // cssVersion (CSS version 1)
    };

    // ========================================================================
    // Instance Variables - Transfer State
    // ========================================================================

    u32 m_nblock_address;
    u32 m_nnumber_blocks;
    u32 m_nbyteCount;
    const char *desc_name = "";
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

    char m_HardwareSerialNumber[20];   // Hardware serial number (e.g., "USBODE-XXXXXXXX")
    const char *m_StringDescriptor[4]; // USB string descriptors
    u8 m_StringDescriptorBuffer[80];   // Buffer for string descriptor conversion

    // Whether to report CSS copy protection
    boolean m_bReportDVDCSS = false;
};

#endif
