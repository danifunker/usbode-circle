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

    // Track Info & Calculation (backed by the gadget's cached track table)
    // pEndLBA optionally receives the LBA right after the track (the next
    // track's start, or the leadout for the last track).
    static CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* gadget, u32 lba, u32 *pEndLBA = nullptr);
    static CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* gadget, int track);
    static int GetLastTrackNumber(CUSBCDGadget* gadget);
    static u32 GetLeadoutLBA(CUSBCDGadget* gadget);

    static int GetBlocksizeForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);
    static int GetSkipbytesForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);

    static int GetMediumType(CUSBCDGadget* gadget);
    static int GetSectorLengthFromMCS(uint8_t mainChannelSelection);
    static int GetSkipBytesFromMCS(uint8_t mainChannelSelection);

    // Standard inter-session gap in frames (lead-out of fromSession plus the
    // next session's lead-in). Must match the gap the image devices bake
    // into their normalized cue sheets.
    static u32 GetSessionGapFrames(int fromSession)
    {
        return (fromSession == 1) ? (6750 + 4500) : (2250 + 4500);
    }
};

#endif
