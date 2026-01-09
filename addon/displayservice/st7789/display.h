#ifndef ST7789_DISPLAY_H
#define ST7789_DISPLAY_H

#include <circle/2dgraphics.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/pwmoutput.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <display/st7789display.h>
#include <displayservice/idisplay.h>
#include <displayservice/pagemanager.h>

class ST7789DiscArtPage;

// Settings for the buttons
#define DEBOUNCETICKS 20
#define ST7789_BUTTONUP 5
#define ST7789_BUTTONDOWN 6
#define ST7789_BUTTONCANCEL 16
#define ST7789_BUTTONOK 24

// Settings for the backlight
#define PWM_CLOCK_RATE 1000000
#define PWM_RANGE 1024
#define DEFAULT_TIMEOUT 10

class ST7789Display : public IDisplay {
   public:
    ST7789Display(DisplayConfig* config, ButtonConfig* buttons);
    virtual ~ST7789Display();

    virtual bool Initialize() override;
    virtual void Clear() override;
    virtual void Sleep() override;
    virtual void Wake() override;
    virtual void Refresh() override;
    virtual bool IsSleeping() override;
    virtual bool Debounce(Button button) override;
    static void HandleButtonPress(void* context);

   private:
    CSPIMaster m_SPIMaster;
    CST7789Display m_Display;
    C2DGraphics m_Graphics;
    PageManager m_PageManager;
    CPWMOutput m_PWMOutput;

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

    int m_backlight_pin;
    CGPIOPin* m_Backlight;
    int backlightTimer;
    int lowPowerTimer;
    bool sleeping = false;
    bool lowPowerMode = false;
    bool pwm_configured = false;
    bool showingSleepWarning = false;
    unsigned sleepWarningStartTime = 0;
    int display_rotation;
    
    void DrawSleepWarning();
    void EnterLowPower();
    void EnterSleep();
    void CheckDiscArtTimer();
    void ShowDiscArt();

    unsigned lastPressTime[static_cast<int>(Button::Count)] = {0};

    // Disc art support
    ST7789DiscArtPage* m_DiscArtPage = nullptr;
    char m_LastDiscPath[512] = {0};
    unsigned m_DiscLoadTime = 0;
    bool m_DiscArtPending = false;
};

#endif  // ST7789DISPLAY_H
