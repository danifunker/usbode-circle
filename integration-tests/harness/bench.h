//
// bench.h
//
// CGadgetTestBench: constructs a real CUSBCDGadget around a fake disc and
// drives it exactly like a USB host does over Bulk-Only Transport:
//
//   SendCommand() writes a CBW into the buffer the gadget queued for the
//   OUT endpoint and fires OnTransferComplete(), then pumps the state
//   machine — completing IN data phases, feeding OUT data phases, and
//   calling Update() for the DataInRead chunked-read path — until the CSW
//   arrives.
//
// Because the bench runs the production dispatch (HandleSCSICommand), unit
// attention gating, residue bookkeeping, and Update() chunking, tests see
// exactly the bytes a host would see on the wire.
//
#ifndef _test_host_bench_h
#define _test_host_bench_h

#include "fakedisc.h"
#include "testbus.h"

#include <discimage/imagedevice.h>

#include <cdplayer/cdplayer.h>
#include <configservice/configservice.h>
#include <scsitbservice/scsitbservice.h>
#include <usbcdgadget/usbcdgadget.h>

#include <vector>

class CGadgetTestBench
{
public:
    struct Result
    {
        std::vector<u8> data; // concatenated data-phase bytes
        TUSBCDCSW csw{};
        bool gotCSW = false;
        bool stalledIn = false;
        bool stalledOut = false;
        int dataChunks = 0; // number of IN data transfers (not counting CSW)
    };

    // Takes ownership of nothing; disc must outlive the bench. Accepts any
    // IImageDevice: the in-memory CFakeImageDevice for command-layer tests,
    // or a real reader (CCueBinFileDevice/CCHDFileDevice/...) for the
    // real-image tests. Optional fakes are registered with the scheduler
    // stub under their production task names before the gadget is constructed.
    CGadgetTestBench(IImageDevice *pDisc,
                     bool bFullSpeed = false,
                     CCDPlayer *pPlayer = nullptr,
                     ConfigService *pConfig = nullptr,
                     SCSITBService *pTBService = nullptr);

    // AddEndpoints + endpoint activation: after this the drive is in
    // UNIT ATTENTION state with a CBW transfer armed, same as right after
    // enumeration on real hardware.
    void Activate();

    Result SendCommand(const u8 *pCDB, size_t nCDBLength, u32 nTransferLength,
                       bool bDirIn = true,
                       const u8 *pOutData = nullptr, size_t nOutLength = 0);

    // Convenience: REQUEST SENSE (also clears unit attention, like a host
    // would after the first CHECK CONDITION).
    Result RequestSense();

    CUSBCDGadget *gadget = nullptr;

private:
    CInterruptSystem m_Interrupt;
    u32 m_nNextTag = 1;
};

#endif
