//
// scsi_toolbox.h
//
// SCSI Toolbox Commands
//
#ifndef _circle_usb_gadget_scsi_toolbox_h
#define _circle_usb_gadget_scsi_toolbox_h

#include <usbcdgadget/usbcdgadget.h>

class SCSIToolbox
{
public:
    static void ListDevices(CUSBCDGadget* gadget);
    static void NumberOfFiles(CUSBCDGadget* gadget);
    static void ListFiles(CUSBCDGadget* gadget);
    static void SetNextCD(CUSBCDGadget* gadget);
};

#endif
