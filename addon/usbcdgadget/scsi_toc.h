//
// scsi_toc.h
//
// SCSI TOC, Disc Info, Track Info
//
#ifndef _circle_usb_gadget_scsi_toc_h
#define _circle_usb_gadget_scsi_toc_h

#include <usbcdgadget/usbcdgadget.h>

class SCSITOC
{
public:
    static void ReadTOC(CUSBCDGadget* gadget);
    static void ReadDiscInformation(CUSBCDGadget* gadget);
    static void ReadTrackInformation(CUSBCDGadget* gadget);
    static void ReadHeader(CUSBCDGadget* gadget);
    static void ReadSubChannel(CUSBCDGadget* gadget);
    static void ReadDiscStructure(CUSBCDGadget* gadget);

private:
    static void DoReadTOC(CUSBCDGadget* gadget, bool msf, uint8_t startingTrack, uint16_t allocationLength);
    static void DoReadSessionInfo(CUSBCDGadget* gadget, bool msf, uint16_t allocationLength);
    static void DoReadFullTOC(CUSBCDGadget* gadget, uint8_t session, uint16_t allocationLength, bool useBCD);
    static void FormatTOCEntry(const CUETrackInfo *track, uint8_t *dest, bool use_MSF);
    static void FormatRawTOCEntry(CUSBCDGadget* gadget, const CUETrackInfo *track, uint8_t *dest, bool useBCD);
    static void DoReadTrackInformation(CUSBCDGadget* gadget, u8 addressType, u32 address, u16 allocationLength);
    static void DoReadHeader(CUSBCDGadget* gadget, bool MSF, uint32_t lba, uint16_t allocationLength);
};

#endif
