//
// Host-build stub for <circle/usb/gadget/dwusbgadget.h>.
// Declares just enough of CDWUSBGadget for CUSBCDGadget to derive from it.
// None of the DWC controller machinery exists here; the test bench drives
// the gadget's callbacks directly.
//
#ifndef _circle_usb_gadget_dwusbgadget_h
#define _circle_usb_gadget_dwusbgadget_h

#include <circle/interrupt.h>
#include <circle/types.h>
#include <circle/usb/usb.h>

#ifndef USB_GADGET_VENDOR_ID
#define USB_GADGET_VENDOR_ID 0x1d6b
#endif

enum TDeviceSpeed
{
    FullSpeed,
    HighSpeed,
    DeviceSpeedUnknown
};

class CDWUSBGadget
{
public:
    CDWUSBGadget(CInterruptSystem *pInterruptSystem, TDeviceSpeed DeviceSpeed);
    virtual ~CDWUSBGadget(void);

protected:
    virtual const void *GetDescriptor(u16 wValue, u16 wIndex, size_t *pLength) = 0;
    virtual void AddEndpoints(void) = 0;
    virtual void CreateDevice(void) = 0;
    virtual void OnSuspend(void) {}
    virtual int OnClassOrVendorRequest(const TSetupData *pSetupData, u8 *pData) { return -1; }
    virtual void OnNegotiatedSpeed(TDeviceSpeed Speed) {}

private:
    TDeviceSpeed m_DeviceSpeed;
};

#endif
