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

    // escsitoolbox `put`: 0xD3 names the destination, 0xD4 carries 512-byte
    // blocks, 0xD5 closes. Restricted here to the Wi-Fi configuration file.
    static void SendFilePrep(CUSBCDGadget* gadget);
    static void SendFile10(CUSBCDGadget* gadget);
    static void SendFileEnd(CUSBCDGadget* gadget);

    // Data-out completion for the three commands above, routed by opcode so
    // that no upload payload reaches the MODE SELECT parser.
    static void ProcessSendFileOut(CUSBCDGadget* gadget, size_t nLength);

    // Drop a half-received upload, e.g. after a USB reset. A queued commit is
    // left alone: it no longer depends on the host.
    static void ResetSendFileState(void);

private:
    // Members rather than file statics because they reach into the gadget's
    // endpoints and sense data, and only this class is a friend of it.
    static void BeginSendFileDataOut(CUSBCDGadget* gadget, u32 nLength);
    static bool FinishSendFile(CUSBCDGadget* gadget);
};

#endif
