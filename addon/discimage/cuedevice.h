#ifndef _ICUEDEVICE_H
#define _ICUEDEVICE_H

#include "imagedevice.h"

class ICueDevice : public IImageDevice {
public:
    ICueDevice() = default;
    virtual ~ICueDevice() = default;
};
#endif
