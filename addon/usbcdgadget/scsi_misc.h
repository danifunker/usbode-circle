//
// scsi_misc.h
//
// SCSI Miscellaneous Commands
//
#ifndef _circle_usb_gadget_scsi_misc_h
#define _circle_usb_gadget_scsi_misc_h

#include <usbcdgadget/usbcdgadget.h>

class SCSIMisc
{
public:
    static void TestUnitReady(CUSBCDGadget* gadget);
    static void StartStopUnit(CUSBCDGadget* gadget);
    static void PreventAllowMediumRemoval(CUSBCDGadget* gadget);
    static void ReadCapacity(CUSBCDGadget* gadget);
    static void MechanismStatus(CUSBCDGadget* gadget);
    static void GetEventStatusNotification(CUSBCDGadget* gadget);
    static void GetPerformance(CUSBCDGadget* gadget);
    static void CommandA4(CUSBCDGadget* gadget);
    static void Verify(CUSBCDGadget* gadget);
    static void SetCDROMSpeed(CUSBCDGadget* gadget);
};

#endif
