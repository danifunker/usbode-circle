#ifndef ST7789_DISPLAY_H
#define ST7789_DISPLAY_H

#include <displayservice/idisplay.h>
#include <displayservice/pagemanager.h>
#include <display/st7789display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/pwmoutput.h>

#define BUTTONUP 5
#define BUTTONDOWN 6
#define BUTTONCANCEL 16
#define BUTTONOK 24

#define PWM_CLOCK_RATE  1000000
#define PWM_RANGE       1024
#define TIMEOUT 10000000

class ST7789Display : public IDisplay {
public:
    ST7789Display(int dc_pin, int reset_pin, int backlight_pin, int spi_cpol, int spi_chpa, int spi_clock_speed, int spi_chip_select);
    virtual ~ST7789Display();

    virtual bool Initialize() override;
    virtual void Clear() override;
    virtual void Sleep() override;
    virtual void Wake() override;
    virtual void Refresh() override;
    virtual bool IsSleeping() override;
    static void HandleButtonPress(void *context);

private:

private:
    CSPIMaster              m_SPIMaster;
    CST7789Display          m_Display;
    C2DGraphics 	    m_Graphics;
    PageManager		    m_PageManager;
    CPWMOutput m_PWMOutput;

    CGPIOManager* m_GPIOManager;
    CGPIOPin* m_ButtonUp;
    CGPIOPin* m_ButtonDown;
    CGPIOPin* m_ButtonOk;
    CGPIOPin* m_ButtonCancel;

    int m_backlight_pin;
    CGPIOPin* m_Backlight;
    int backlightTimer;
    bool sleeping = false;
};

#endif // ST7789DISPLAY_H

