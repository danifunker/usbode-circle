//
// Host-build stub for <tracelab/tracelab.h>.
// All trace calls are no-ops; Get() always returns a valid singleton, like
// on the device after CUSBCDGadget's constructor runs.
//
#ifndef _tracelab_tracelab_h
#define _tracelab_tracelab_h

#include <circle/types.h>

class CTraceLab
{
public:
    CTraceLab(void) {}

    static CTraceLab *Get(void)
    {
        static CTraceLab instance;
        return &instance;
    }

    boolean Initialize(void) { return TRUE; }

    void TraceCDBReceived(u8 lun, const u8 *pCDB, u8 nCDBLength) {}
    void TraceCommandComplete(u8 opcode, u8 status, u32 residue) {}
    void TraceSenseSet(u8 senseKey, u8 asc, u8 ascq) {}
    void TraceMediaState(u8 fromState, u8 toState) {}
    void TraceUSBSuspend(void) {}
    void TraceUSBActivate(void) {}
    void TraceUSBSpeed(boolean bFullSpeed) {}
    void TraceImageReadStart(u32 lba, u32 bytes) {}
    void TraceImageReadComplete(u32 lba, u32 bytesRead) {}
    void TraceImageReadError(u32 lba, u32 bytes) {}
    void TraceTransferStart(u32 bytes) {}
    void TraceTransferComplete(u32 bytes) {}
};

#endif
