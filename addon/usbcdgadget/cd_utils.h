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
    static uint8_t ToBCD(int value);
    static int32_t MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative);
    static u32 GetAddress(u32 lba, int msf, boolean relative);
    static u32 lba_to_msf(u32 lba, boolean relative = false);
    static u32 msf_to_lba(u8 minutes, u8 seconds, u8 frames);

    // Track Info & Calculation
    static CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* gadget, u32 lba);
    static CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* gadget, int track);
    static int GetLastTrackNumber(CUSBCDGadget* gadget);
    static u32 GetLeadoutLBA(CUSBCDGadget* gadget);

    // Session layout. A CD Extra / Enhanced CD holds its audio tracks in
    // session 1 and a single data track in session 2, and hosts locate the
    // filesystem through the session structure rather than the track list.
    static int GetSessionCount(CUSBCDGadget* gadget);
    static int GetLastSessionStartTrack(CUSBCDGadget* gadget);
    static u32 GetSessionLeadoutLBA(CUSBCDGadget* gadget, int session);

    static int GetBlocksize(CUSBCDGadget* gadget);
    static int GetBlocksizeForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);

    static int GetSkipbytes(CUSBCDGadget* gadget);
    static int GetSkipbytesForTrack(CUSBCDGadget* gadget, CUETrackInfo trackInfo);

    static int GetMediumType(CUSBCDGadget* gadget);

    // Sizes of the five fields of one sector, in the order they appear on the
    // disc. A field that this sector kind does not have is zero: Mode 1 has no
    // subheader, CD-DA has nothing but user data. See cd_utils.cpp.
    struct TCDSectorShape
    {
        int nSync;
        int nHeader;
        int nSubheader;
        int nUserData;
        int nEdcEcc;
    };

    static TCDSectorShape GetSectorShape(int expectedSectorType, CUETrackMode trackMode);
    static bool McsFieldsAreContiguous(uint8_t mainChannelSelection, const TCDSectorShape& shape);
    static int GetSectorLengthFromMCS(uint8_t mainChannelSelection, const TCDSectorShape& shape);
    static int GetSkipBytesFromMCS(uint8_t mainChannelSelection, const TCDSectorShape& shape);
};

#endif
