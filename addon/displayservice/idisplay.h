#ifndef IDISPLAY_H
#define IDISPLAY_H

#include "pagemanager.h"

struct DisplayConfig {
    unsigned dc_pin;
    unsigned reset_pin;
    unsigned backlight_pin;
    unsigned spi_cpol;
    unsigned spi_cpha;
    unsigned spi_clock_speed;
    unsigned spi_chip_select;
};

// This interface represents all display. It exists so we
// can support different display types (waveshare, st7789, ...)
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
