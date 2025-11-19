#ifndef _IIMAGEDEVICE_H
#define _IIMAGEDEVICE_H

#include <circle/device.h>

enum class IMAGE_TYPE {
    UNKNOWN,
    ISO,
    CUE,
    MDS,
    NRG,
    MDX,
};

class IImageDevice : public CDevice {
public:
    IImageDevice() = default;
    virtual ~IImageDevice() = default;

    virtual MEDIA_TYPE GetMediaType() const = 0;
    virtual const char* GetCueSheet() const = 0;
};

#endif
