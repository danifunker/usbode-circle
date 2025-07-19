#ifndef IDISPLAY_H
#define IDISPLAY_H

#include "pagemanager.h"

class IDisplay {
public:
    virtual ~IDisplay() = default;

    virtual bool Initialize() = 0;
    virtual void Clear() = 0;
    virtual void Sleep() = 0;
    virtual void Wake() = 0;
    virtual void Refresh() = 0;
    virtual bool IsSleeping() = 0;
    virtual bool Debounce(Button button) = 0;
};

#endif
