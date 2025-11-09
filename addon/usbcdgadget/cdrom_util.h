#ifndef _cdrom_util_h
#define _cdrom_util_h

#include <circle/types.h>
#include <cueparser/cueparser.h>
#include <discimage/cuebinfile.h>

class CUSBCDGadget;

u8 BCD(u8 val);
u32 msf_to_lba(u8 m, u8 s, u8 f);
void LBA2MSF(u32 lba, u8 *m, boolean is_bcd);
void LBA2MSFBCD(u32 lba, u8 *m, boolean is_bcd);
u32 GetAddress(u32 address, bool msf, bool relative);

CUETrackInfo GetTrackInfoForLBA(CUSBCDGadget* pGadget, u32 lba);
CUETrackInfo GetTrackInfoForTrack(CUSBCDGadget* pGadget, int track);
int GetLastTrackNumber(CUSBCDGadget* pGadget);
u32 GetLeadoutLBA(CUSBCDGadget* pGadget);
int GetBlocksize(CUSBCDGadget* pGadget);
int GetBlocksizeForTrack(CUETrackInfo trackInfo);
int GetSkipbytes(CUSBCDGadget* pGadget);
int GetSkipbytesForTrack(CUETrackInfo trackInfo);
int GetMediumType(CUSBCDGadget* pGadget);

#endif
