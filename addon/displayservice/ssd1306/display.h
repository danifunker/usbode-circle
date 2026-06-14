/*
/ (c) 2025
/ MT32-Pi-compatible HAT display driver for USBODE.
/ Drives an I2C SSD1306 128x64 (or 128x32) OLED and the four tactile
/ buttons found on mt32-pi style HATs (e.g. chris-jh/mt32-pi-midi-hat).
/
/ The PCM5102A I2S DAC on these HATs is handled by the existing CDPlayer
/ "sndi2s" path and needs no support here.
*/
#ifndef MT32PI_DISPLAY_H
#define MT32PI_DISPLAY_H

#include <circle/2dgraphics.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/i2cmaster.h>
#include <configservice/configservice.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/idisplay.h>
#include <displayservice/pagemanager.h>

// Button debounce window (timer ticks), matching the SH1106 driver
#define MT32PI_DEBOUNCETICKS 20

// Default control-surface button BCM GPIO pins.
// mt32-pi's simple control surface uses GPIO 17/27 for buttons and 22/23 for
// the rotary encoder; we map four tactile buttons to Up/Down/Ok/Cancel here.
// All are overridable via the [mt32pi] config section.
#define MT32PI_BUTTONUP 17
#define MT32PI_BUTTONDOWN 27
#define MT32PI_BUTTONOK 23
#define MT32PI_BUTTONCANCEL 22

#define MT32PI_DEFAULT_TIMEOUT 10
#define MT32PI_SLEEP_WARNING_DURATION (2 * 1000)  // 2 seconds in milliseconds

// Extra configuration specific to the I2C SSD1306 display.
struct SSD1306Config {
    u8 i2c_address;
    unsigned oled_width;
    unsigned oled_height;
};

class MT32PiDisplay : public IDisplay {
   public:
    MT32PiDisplay(SSD1306Config* config, ButtonConfig* buttons);
    virtual ~MT32PiDisplay();

    virtual bool Initialize() override;
    virtual void Clear() override;
    virtual void Sleep() override;
    virtual void Wake() override;
    virtual void Refresh() override;
    virtual bool IsSleeping() override;
    virtual bool Debounce(Button button) override;
    static void HandleButtonPress(void* context);

   private:
    void DrawSleepWarning();
    // Drain any button/wake requests queued by the interrupt handler.
    // Runs in task context so display I2C is never touched from an IRQ.
    void ProcessPendingInput();

   private:
    CI2CMaster m_I2CMaster;
    CSSD1306GfxDisplay m_Display;
    C2DGraphics m_Graphics;
    PageManager m_PageManager;

    CGPIOManager* m_GPIOManager;
    CGPIOPin* m_ButtonUp;
    CGPIOPin* m_ButtonDown;
    CGPIOPin* m_ButtonOk;
    CGPIOPin* m_ButtonCancel;

    const int up_pin;
    const int down_pin;
    const int ok_pin;
    const int cancel_pin;

    ConfigService* configservice;

    int backlightTimer;
    bool sleeping = false;

    // Set by the GPIO interrupt handler, consumed in task context by
    // ProcessPendingInput(). The SSD1306 talks over I2C, whose Circle driver
    // uses a TASK_LEVEL spinlock and shares the bus with the CDPlayer DAC, so
    // performing display I/O directly from an IRQ can deadlock. We therefore
    // only flag work here and do the actual I2C in Refresh().
    volatile bool m_PendingButton[static_cast<int>(Button::Count)] = {false};

    unsigned lastPressTime[static_cast<int>(Button::Count)] = {0};
};

#endif  // MT32PI_DISPLAY_H
