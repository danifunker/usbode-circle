//
// Host-build stub for <circle/device.h>.
//
#ifndef _circle_device_h
#define _circle_device_h

#include <circle/types.h>

class CDevice
{
public:
    virtual ~CDevice(void) {}

    virtual int Read(void *pBuffer, size_t nCount) { return -1; }
    virtual int Write(const void *pBuffer, size_t nCount) { return -1; }
};

#endif
