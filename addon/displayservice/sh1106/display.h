#ifndef SH1106_DISPLAY_H
#define SH1106_DISPLAY_H

#include <circle/2dgraphics.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/pwmoutput.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/idisplay.h>
#include <displayservice/pagemanager.h>

// Settings for the buttons
#define DEBOUNCETICKS 20

#define SH1106_BUTTONUP 6
#define SH1106_BUTTONDOWN 19
#define SH1106_BUTTONLEFT 5
#define SH1106_BUTTONRIGHT 26
#define SH1106_BUTTONCANCEL 20
#define SH1106_BUTTONOK 21
#define SH1106_BUTTONCENTER 13
#define SH1106_BUTTONKEY3 16

//#define SH1106_BUTTONUP 5
//#define SH1106_BUTTONDOWN 6
//#define SH1106_BUTTONCANCEL 16
//#define SH1106_BUTTONOK 24

#define DEFAULT_TIMEOUT 10
#define SLEEP_WARNING_DURATION 2 * 1000  // 2 seconds in milliseconds

class SH1106Display : public IDisplay {
   public:
    SH1106Display(DisplayConfig* config, ButtonConfig* buttons);
    virtual ~SH1106Display();

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

   private:
    CSPIMaster m_SPIMaster;
    CSH1106Display m_Display;
    C2DGraphics m_Graphics;
    PageManager m_PageManager;

    CGPIOManager* m_GPIOManager;
    CGPIOPin* m_ButtonUp;
    CGPIOPin* m_ButtonDown;
    CGPIOPin* m_ButtonOk;
    CGPIOPin* m_ButtonCancel;
    CGPIOPin* m_ButtonLeft;
    CGPIOPin* m_ButtonRight;
    CGPIOPin* m_ButtonKey3;
    CGPIOPin* m_ButtonCenter;

    const int up_pin;
    const int down_pin;
    const int ok_pin;
    const int cancel_pin;
    const int left_pin;
    const int right_pin;
    const int center_pin;
    const int key3_pin;

    ConfigService* configservice;

    int backlightTimer;
    bool sleeping = false;
    int display_rotation;

    unsigned lastPressTime[static_cast<int>(Button::Count)] = {0};
};

#endif  // SH1106DISPLAY_H
