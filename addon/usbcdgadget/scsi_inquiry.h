//
// scsi_inquiry.h
//
// SCSI Inquiry, Mode Sense, Request Sense
//
#ifndef _circle_usb_gadget_scsi_inquiry_h
#define _circle_usb_gadget_scsi_inquiry_h

#include <usbcdgadget/usbcdgadget.h>

class SCSIInquiry
{
public:
    static void Inquiry(CUSBCDGadget* gadget);
    static void RequestSense(CUSBCDGadget* gadget);
    static void ModeSense6(CUSBCDGadget* gadget);
    static void ModeSense10(CUSBCDGadget* gadget);
    static void GetConfiguration(CUSBCDGadget* gadget);
    static void ModeSelect10(CUSBCDGadget* gadget);

private:
    static void FillModePage(CUSBCDGadget* gadget, u8 page, u8 *buffer, int &length);
};

#endif
