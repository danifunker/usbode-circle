#ifndef _circle_usb_gadget_usb_structs_h
#define _circle_usb_gadget_usb_structs_h

#include <circle/types.h>

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

#endif
