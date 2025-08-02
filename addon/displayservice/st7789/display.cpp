
/*
/ (c) 2025 Ian Cass
/ This is a display driver for USBODE for the ST7789 series screens
/ It's responsible for managing the page rendering
*/
#include "display.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <displayservice/buttonhandler.h>
#include <displayservice/buttons.h>

#include "configpage.h"
#include "homepage.h"
#include "imagespage.h"
#include "infopage.h"
#include "logconfigpage.h"
#include "timeoutconfigpage.h"
#include "powerpage.h"
#include "splashpage.h"
#include "usbconfigpage.h"

LOGMODULE("st7789display");

// Constructor
ST7789Display::ST7789Display(DisplayConfig* config, ButtonConfig* buttons)
    : m_SPIMaster(config->spi_clock_speed, config->spi_cpol, config->spi_cpha, 0),
      m_Display(&m_SPIMaster, config->dc_pin, config->reset_pin, 0, 240, 240,
                config->spi_cpol, config->spi_cpha, config->spi_clock_speed, config->spi_chip_select),
      m_Graphics(&m_Display),
      m_PWMOutput(PWM_CLOCK_RATE, PWM_RANGE, true),
      up_pin(buttons->Up),
      down_pin(buttons->Down),
      ok_pin(buttons->Ok),
      cancel_pin(buttons->Cancel)
{
    // Obtain our config service
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    // Initialize the backlight variables
    if (config->backlight_pin)
        m_backlight_pin = config->backlight_pin;

    backlightTimer = CTimer::Get()->GetClockTicks();

    LOGNOTE("Started ST7789 Display");
}

// Destructor
ST7789Display::~ST7789Display() {
    delete m_ButtonUp;
    delete m_ButtonDown;
    delete m_ButtonOk;
    delete m_ButtonCancel;
    delete m_Backlight;
    delete m_GPIOManager;

    LOGNOTE("ST7789Display resources released.");
}

bool ST7789Display::Initialize() {
    // TODO move this to a base class?
    bool bOK = true;
    if (bOK) {
        bOK = m_SPIMaster.Initialize();
        LOGNOTE("Initialized SPI");
    }

    if (bOK) {
        bOK = m_Display.Initialize();
        // TODO: expose this as a config entry
        m_Display.SetRotation(270);
        LOGNOTE("Initialized ST7789 Display");
    }

    if (bOK) {
        bOK = m_Graphics.Initialize();
        LOGNOTE("Initialized Graphics");
    }

    // register pages
    m_PageManager.RegisterPage("splashpage", new ST7789SplashPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("homepage", new ST7789HomePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("imagespage", new ST7789ImagesPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("powerpage", new ST7789PowerPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("configpage", new ST7789ConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("usbconfigpage", new ST7789USBConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("logconfigpage", new ST7789LogConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("timeoutconfigpage", new ST7789TimeoutConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("infopage", new ST7789InfoPage(&m_Display, &m_Graphics));

    // Set the stating page
    m_PageManager.SetActivePage("splashpage");
    LOGNOTE("Registered pages");

    // register buttons
    // TODO: move to base class
    CInterruptSystem* interruptSystem = CInterruptSystem::Get();
    m_GPIOManager = new CGPIOManager(interruptSystem);
    if (bOK) {
        bOK = m_GPIOManager->Initialize();
        LOGNOTE("Initialized GPIO Manager");
    }

    // TODO move to base class
    if (bOK) {
        m_ButtonUp = new CGPIOPin(up_pin, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonUpCtx = {this, &m_PageManager, m_ButtonUp, Button::Up};
        m_ButtonUp->ConnectInterrupt(HandleButtonPress, &buttonUpCtx);
        m_ButtonUp->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonDown = new CGPIOPin(down_pin, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonDownCtx = {this, &m_PageManager, m_ButtonDown, Button::Down};
        m_ButtonDown->ConnectInterrupt(HandleButtonPress, &buttonDownCtx);
        m_ButtonDown->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonOk = new CGPIOPin(ok_pin, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonOkCtx = {this, &m_PageManager, m_ButtonOk, Button::Ok};
        m_ButtonOk->ConnectInterrupt(HandleButtonPress, &buttonOkCtx);
        m_ButtonOk->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonCancel = new CGPIOPin(cancel_pin, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonCancelCtx = {this, &m_PageManager, m_ButtonCancel, Button::Cancel};
        m_ButtonCancel->ConnectInterrupt(HandleButtonPress, &buttonCancelCtx);
        m_ButtonCancel->EnableInterrupt(GPIOInterruptOnFallingEdge);
        LOGNOTE("Registered buttons");
    }

    if (bOK) {
        m_Backlight = new CGPIOPin(m_backlight_pin, GPIOModeAlternateFunction0, m_GPIOManager);
        m_PWMOutput.Start();
        m_PWMOutput.Write(2, 1024);
        pwm_configured = true;
    }

    return bOK;
}

void ST7789Display::Clear() {
    // TODO: Clear screen. Do we need this?
}

// Dim the screen or even turn it off
void ST7789Display::Sleep() {
    if (!pwm_configured)
        return;

    LOGNOTE("Sleeping");
    sleeping = true;
    m_PWMOutput.Write(2, 32);  // TODO make the backlight value configurable
}

// Wake the screen
void ST7789Display::Wake() {
    backlightTimer = CTimer::Get()->GetClockTicks();
    if (sleeping) {
        m_PWMOutput.Write(2, 1024);
        LOGNOTE("Waking");
    }
    sleeping = false;
}

bool ST7789Display::IsSleeping() {
    return sleeping;
}

// Called by displaymanager kernel loop. Check backlight timeout and sleep if
// necessary. Pass on the refresh call to the page manager
void ST7789Display::Refresh() {
    unsigned backlightTimeout = configservice->GetScreenTimeout(DEFAULT_TIMEOUT) * 1000000;
    if (!backlightTimeout && sleeping)
            Wake();

    if (backlightTimeout) {
	    unsigned now = CTimer::Get()->GetClockTicks();
	    // LOGNOTE("backlightTimer is %d, now is %d, TIMEOUT is %d", backlightTimer, now, backlightTimeout);
	    // Is it time to dim the screen?
	    if (!sleeping && now - backlightTimer > backlightTimeout)
		Sleep();
    }

    m_PageManager.Refresh();
}

// Debounce the key presses
bool ST7789Display::Debounce(Button button) {
    unsigned now = CTimer::Get()->GetTicks();  // TODO. Do we need to use GetClockTicks instead?
    if (now - lastPressTime[(int)button] < DEBOUNCETICKS) {
        LOGNOTE("Ignored a bounce!");
        return true;
    }

    lastPressTime[(int)button] = now;
    return false;
}

// This is the callback from the GPIO Button interrupt. Debounce, wake screen on button press,
// and then pass on the keypress to page manager (and ultimately the page) to handle
void ST7789Display::HandleButtonPress(void* pParam) {
    ButtonHandlerContext* context = static_cast<ButtonHandlerContext*>(pParam);
    LOGNOTE("Got button press %d", context->button);
    if (context) {
        if (context->display->Debounce(context->button))
            return;

        bool wasSleeping = context->display->IsSleeping();
        context->display->Wake();

        // If it was sleeping, swallow this keypress
        if (wasSleeping)
            return;

        context->pin->DisableInterrupt();
        context->pageManager->HandleButtonPress(context->button);
        context->pin->EnableInterrupt(GPIOInterruptOnFallingEdge);
    }
}
