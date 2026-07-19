//
// Host-build stub for <circle/usb/gadget/dwusbgadgetendpoint.h>.
// BeginTransfer()/Stall() record their arguments in the test bus sink
// (see harness/stubs.cpp) instead of touching hardware. The test bench
// inspects the sink and calls OnTransferComplete() to emulate the host.
//
#ifndef _circle_usb_gadget_dwusbgadgetendpoint_h
#define _circle_usb_gadget_dwusbgadgetendpoint_h

#include <circle/types.h>
#include <circle/usb/usb.h>

class CDWUSBGadget;

class CDWUSBGadgetEndpoint
{
public:
    enum TTransferMode
    {
        TransferDataOut,
        TransferDataIn
    };

    enum TDirection
    {
        DirectionOut,
        DirectionIn
    };

    CDWUSBGadgetEndpoint(const TUSBEndpointDescriptor *pDesc, CDWUSBGadget *pGadget);
    virtual ~CDWUSBGadgetEndpoint(void);

    virtual void OnActivate(void) = 0;
    virtual void OnDeactivate(void) = 0;
    virtual void OnTransferComplete(boolean bIn, size_t nLength) = 0;

    void BeginTransfer(TTransferMode Mode, void *pBuffer, size_t nLength);
    void Stall(boolean bIn);
    void SetMaxPacketSize(size_t nSize);

    TDirection GetDirection(void) const { return m_Direction; }

private:
    TDirection m_Direction;
    size_t m_nMaxPacketSize;
};

#endif
