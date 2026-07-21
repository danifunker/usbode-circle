//
// bench.cpp
//
#include "bench.h"

#include <circle/sched/scheduler.h>
#include <circle/timer.h>

#include <assert.h>
#include <string.h>

CGadgetTestBench::CGadgetTestBench(IImageDevice *pDisc, bool bFullSpeed,
                                   CCDPlayer *pPlayer, ConfigService *pConfig,
                                   SCSITBService *pTBService)
{
    TestBus::Get().Reset();
    CTimer::Get()->TestReset();
    CScheduler::Get()->TestClearTasks();

    if (pPlayer != nullptr)
    {
        CScheduler::Get()->TestRegisterTask("cdplayer", pPlayer);
    }
    if (pConfig != nullptr)
    {
        CScheduler::Get()->TestRegisterTask("configservice", pConfig);
    }
    if (pTBService != nullptr)
    {
        CScheduler::Get()->TestRegisterTask("scsitbservice", pTBService);
    }

    // The gadget's destructor intentionally asserts (it must never be
    // destroyed on the device), so bench instances leak it. Tests are
    // short-lived processes; that is fine.
    //
    // Construct with nullptr and attach the disc via SetDevice(),
    // mirroring CDROMService::Initialize(). Passing the device to the
    // constructor takes SetDevice()'s eject path with m_pDevice == dev
    // and deletes the device it was just given (latent use-after-free;
    // production never uses that path).
    gadget = new CUSBCDGadget(&m_Interrupt, bFullSpeed, nullptr);
    gadget->SetDevice(pDisc);
}

void CGadgetTestBench::Activate()
{
    gadget->AddEndpoints();

    // Real hardware: the OUT endpoint's OnActivate() forwards to the
    // gadget and arms the audio-init flag.
    gadget->m_pEP[CUSBCDGadget::EPOut]->OnActivate();
}

CGadgetTestBench::Result CGadgetTestBench::SendCommand(const u8 *pCDB, size_t nCDBLength,
                                                       u32 nTransferLength, bool bDirIn,
                                                       const u8 *pOutData, size_t nOutLength)
{
    Result result;
    TestBus &bus = TestBus::Get();

    bus.inStalled = false;
    bus.outStalled = false;

    // The gadget must be waiting for a CBW (armed by OnActivate or by the
    // completion of the previous CSW).
    assert(bus.outTransfer.valid && "gadget is not waiting for a CBW");

    TUSBCDCBW cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = VALID_CBW_SIG;
    cbw.dCBWTag = m_nNextTag++;
    cbw.dCBWDataTransferLength = nTransferLength;
    cbw.bmCBWFlags = bDirIn ? 0x80 : 0x00;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = (u8)nCDBLength;
    memcpy(cbw.CBWCB, pCDB, nCDBLength);

    memcpy(bus.outTransfer.buffer, &cbw, SIZE_CBW);
    bus.outTransfer.valid = false;
    gadget->OnTransferComplete(FALSE, SIZE_CBW);

    Pump(result, pOutData, nOutLength);
    return result;
}

CGadgetTestBench::Result CGadgetTestBench::SendRawCBW(const void *pData, size_t nLength)
{
    Result result;
    TestBus &bus = TestBus::Get();

    bus.inStalled = false;
    bus.outStalled = false;

    assert(bus.outTransfer.valid && "gadget is not waiting for a CBW");
    assert(nLength <= bus.outTransfer.length);

    memcpy(bus.outTransfer.buffer, pData, nLength);
    bus.outTransfer.valid = false;
    gadget->OnTransferComplete(FALSE, nLength);

    Pump(result, nullptr, 0);
    return result;
}

void CGadgetTestBench::Pump(Result &result, const u8 *pOutData, size_t nOutLength)
{
    TestBus &bus = TestBus::Get();

    // Pump the state machine until the CSW has been transferred.
    for (int guard = 0; guard < 100000; guard++)
    {
        if (bus.inTransfer.valid)
        {
            TestBus::Pending t = bus.inTransfer;
            bus.inTransfer.valid = false;

            if (gadget->m_nState == CUSBCDGadget::TCDState::SentCSW)
            {
                assert(t.length == SIZE_CSW);
                memcpy(&result.csw, t.buffer, SIZE_CSW);
                result.gotCSW = true;
                gadget->OnTransferComplete(TRUE, t.length); // re-arms CBW
                break;
            }

            const u8 *p = (const u8 *)t.buffer;
            result.data.insert(result.data.end(), p, p + t.length);
            result.dataChunks++;
            gadget->OnTransferComplete(TRUE, t.length);
            continue;
        }

        if (bus.outTransfer.valid && gadget->m_nState == CUSBCDGadget::TCDState::DataOut)
        {
            size_t n = nOutLength;
            if (n > bus.outTransfer.length)
            {
                n = bus.outTransfer.length;
            }
            if (pOutData != nullptr && n > 0)
            {
                memcpy(bus.outTransfer.buffer, pOutData, n);
            }
            bus.outTransfer.valid = false;
            gadget->OnTransferComplete(FALSE, n);
            continue;
        }

        if (gadget->m_nState == CUSBCDGadget::TCDState::DataInRead)
        {
            gadget->Update();
            continue;
        }

        break; // no progress possible
    }

    result.stalledIn = bus.inStalled;
    result.stalledOut = bus.outStalled;
}

CGadgetTestBench::Result CGadgetTestBench::RequestSense()
{
    const u8 cdb[6] = {0x03, 0x00, 0x00, 0x00, 18, 0x00};
    return SendCommand(cdb, sizeof(cdb), 18);
}
