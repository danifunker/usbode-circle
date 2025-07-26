
/*
/ (c) 2025 Ian Cass
/ (c) 2025 Dani Sarfati
/ This is a display driver for USBODE for the SH1106 series screens
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
#include "powerpage.h"
#include "usbconfigpage.h"

LOGMODULE("kernel");

SH1106Display::SH1106Display(int dc_pin, int reset_pin, int backlight_pin, int spi_cpol, int spi_cpha, int spi_clock_speed, int spi_chip_select)
    : m_SPIMaster(spi_clock_speed, spi_cpol, spi_cpha, 0),
      m_Display(&m_SPIMaster, dc_pin, reset_pin, 128, 64,
		spi_clock_speed, spi_cpol, spi_cpha, spi_chip_select),
      m_Graphics(&m_Display) 
{
	     
    // Obtain our config service
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    backlightTimer = CTimer::Get()->GetClockTicks();

    LOGNOTE("Started SH1106Display Display");
}

// Destructor
SH1106Display::~SH1106Display() {
    delete m_ButtonUp;
    delete m_ButtonDown;
    delete m_ButtonOk;
    delete m_ButtonCancel;
    delete m_GPIOManager;

    LOGNOTE("SH1106Display resources released.");
}

bool SH1106Display::Initialize() {
    bool bOK = true;
    if (bOK) {
        bOK = m_SPIMaster.Initialize();
        LOGNOTE("Initialized SPI");
    }

    if (bOK) {
        bOK = m_Display.Initialize();
        // TODO: expose this as a config entry
        //m_Display.SetRotation(270);
        //LOGNOTE("Initialized SH1106 Display");
    }

    if (bOK) {
        bOK = m_Graphics.Initialize();
        LOGNOTE("Initialized Graphics");
    }

    // register pages
    m_PageManager.RegisterPage("homepage", new SH1106HomePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("imagespage", new SH1106ImagesPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("powerpage", new SH1106PowerPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("configpage", new SH1106ConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("usbconfigpage", new SH1106USBConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("logconfigpage", new SH1106LogConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("infopage", new SH1106InfoPage(&m_Display, &m_Graphics));

    // Set the stating page
    m_PageManager.SetActivePage("homepage");
    LOGNOTE("Registered pages");

    // register buttons
    CInterruptSystem* interruptSystem = CInterruptSystem::Get();
    m_GPIOManager = new CGPIOManager(interruptSystem);
    if (bOK) {
        bOK = m_GPIOManager->Initialize();
        LOGNOTE("Initialized GPIO Manager");
    }

    if (bOK) {
        m_ButtonUp = new CGPIOPin(SH1106_BUTTONUP, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonUpCtx = {this, &m_PageManager, m_ButtonUp, Button::Up};
        m_ButtonUp->ConnectInterrupt(HandleButtonPress, &buttonUpCtx);
        m_ButtonUp->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonDown = new CGPIOPin(SH1106_BUTTONDOWN, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonDownCtx = {this, &m_PageManager, m_ButtonDown, Button::Down};
        m_ButtonDown->ConnectInterrupt(HandleButtonPress, &buttonDownCtx);
        m_ButtonDown->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonOk = new CGPIOPin(SH1106_BUTTONOK, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonOkCtx = {this, &m_PageManager, m_ButtonOk, Button::Ok};
        m_ButtonOk->ConnectInterrupt(HandleButtonPress, &buttonOkCtx);
        m_ButtonOk->EnableInterrupt(GPIOInterruptOnFallingEdge);

        m_ButtonCancel = new CGPIOPin(SH1106_BUTTONCANCEL, GPIOModeInputPullUp, m_GPIOManager);
        static ButtonHandlerContext buttonCancelCtx = {this, &m_PageManager, m_ButtonCancel, Button::Cancel};
        m_ButtonCancel->ConnectInterrupt(HandleButtonPress, &buttonCancelCtx);
        m_ButtonCancel->EnableInterrupt(GPIOInterruptOnFallingEdge);
        LOGNOTE("Registered buttons");
    }

    // Backlight timeout
    backlightTimeout = config->GetScreenTimeout(DEFAULT_TIMEOUT) * 1000000;
    LOGNOTE("Registered backlight");

    return bOK;
}

void SH1106Display::Clear() {
    // TODO: Clear screen. Do we need this?
}

// Dim the screen or even turn it off
void SH1106Display::Sleep() {
    LOGNOTE("Sleeping");
    sleeping = true;
    m_Display.Off();
}

// Wake the screen
void SH1106Display::Wake() {
    backlightTimer = CTimer::Get()->GetClockTicks();
    if (sleeping) {
	m_Display.On();
        LOGNOTE("Waking");
    }
    sleeping = false;
}

bool SH1106Display::IsSleeping() {
    return sleeping;
}

// Called by displaymanager kernel loop. Check backlight timeout and sleep if
// necessary. Pass on the refresh call to the page manager
void SH1106Display::Refresh() {
    // Is it time to dim the screen?
    unsigned now = CTimer::Get()->GetClockTicks();
    // LOGNOTE("backlightTimer is %d, now is %d, TIMEOUT is %d", backlightTimer, now, TIMEOUT);
    if (!sleeping && now - backlightTimer > backlightTimeout)
        Sleep();

    m_PageManager.Refresh();
}

// Debounce the key presses
bool SH1106Display::Debounce(Button button) {
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
void SH1106Display::HandleButtonPress(void* pParam) {
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
