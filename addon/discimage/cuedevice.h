#ifndef _ICUEDEVICE_H
#define _ICUEDEVICE_H

#include "imagedevice.h"

class ICueDevice : public IImageDevice {
public:
    ICueDevice() = default;
    virtual ~ICueDevice() = default;

    /// \return Current offset in the device, (u64)-1 on error
    virtual u64 Tell() const = 0;
};
#endif
