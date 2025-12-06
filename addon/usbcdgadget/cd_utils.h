//
// cd_utils.h
//
// CD-ROM Utility Functions and Calculations
//
#ifndef _circle_usb_gadget_cd_utils_h
#define _circle_usb_gadget_cd_utils_h

#include <usbcdgadget/usbcdgadget.h>

class CDUtils
{
public:
    // Address Conversion Utilities
    static void LBA2MSF(int32_t LBA, uint8_t *MSF, bool relative);
    static void LBA2MSFBCD(int32_t LBA, uint8_t *MSF, bool relative);
    static int32_t MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative);
    static u32 GetAddress(u32 lba, int msf, boolean relative);
    static u32 lba_to_msf(u32 lba, boolean relative = false);
    static u32 msf_to_lba(u8 minutes, u8 seconds, u8 frames);

    // Track Info & Calculation
    static CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* gadget, u32 lba);
    static CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* gadget, int track);
    static int GetLastTrackNumber(CUSBCDGadget* gadget);
    static u32 GetLeadoutLBA(CUSBCDGadget* gadget);

    static int GetBlocksize(CUSBCDGadget* gadget);
    static int GetBlocksizeForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);

    static int GetSkipbytes(CUSBCDGadget* gadget);
    static int GetSkipbytesForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);

    static int GetMediumType(CUSBCDGadget* gadget);
    static int GetSectorLengthFromMCS(uint8_t mainChannelSelection);
    static int GetSkipBytesFromMCS(uint8_t mainChannelSelection);
};

#endif
