//
// Host-build stub for <circle/bcmpropertytags.h>.
// GetTag() always fails, so the gadget falls back to its default serial
// number ("USBODE-00000001") — deterministic for tests.
//
#ifndef _circle_bcmpropertytags_h
#define _circle_bcmpropertytags_h

#include <circle/types.h>

#define PROPTAG_GET_BOARD_SERIAL 0x00010004

struct TPropertyTagSerial
{
    u32 Serial[2];
};

class CBcmPropertyTags
{
public:
    boolean GetTag(u32 nTagId, void *pTag, unsigned nTagSize, unsigned nRequestParmSize = 0)
    {
        return FALSE;
    }
};

#endif
