//
// scsi_read.h
//
// SCSI Read, Play Audio, Seek, Pause/Resume, Stop/Scan
//
#ifndef _circle_usb_gadget_scsi_read_h
#define _circle_usb_gadget_scsi_read_h

#include <usbcdgadget/usbcdgadget.h>

class SCSIRead
{
public:
    static void Read10(CUSBCDGadget* gadget);
    static void Read12(CUSBCDGadget* gadget);
    static void PlayAudio10(CUSBCDGadget* gadget);
    static void PlayAudio12(CUSBCDGadget* gadget);
    static void PlayAudioMSF(CUSBCDGadget* gadget);
    static void Seek(CUSBCDGadget* gadget);
    static void PauseResume(CUSBCDGadget* gadget);
    static void StopScan(CUSBCDGadget* gadget);
    static void ReadCD(CUSBCDGadget* gadget);

private:
    static void DoRead(CUSBCDGadget* gadget, int cdbSize);
    static void DoPlayAudio(CUSBCDGadget* gadget, int cdbSize);
};

#endif
