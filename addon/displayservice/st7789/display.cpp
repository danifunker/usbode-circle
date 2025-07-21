#include "display.h"
#include <circle/logger.h>
#include "homepage.h"
#include "splashpage.h"
#include "imagespage.h"
#include "powerpage.h"
#include "configpage.h"
#include "usbconfigpage.h"
#include <displayservice/buttons.h>
#include <circle/timer.h>
#include <displayservice/buttonhandler.h>
#include <circle/sched/scheduler.h>

LOGMODULE("kernel");

// Constructor
ST7789Display::ST7789Display(int dc_pin, int reset_pin, int backlight_pin, int spi_cpol, int spi_cpha, int spi_clock_speed, int spi_chip_select) 
:         m_SPIMaster (spi_clock_speed, spi_cpol, spi_cpha, 0),
        m_Display (&m_SPIMaster, dc_pin, reset_pin, 0, 240, 240,
                   spi_cpol, spi_cpha, spi_clock_speed, spi_chip_select),
	m_Graphics(&m_Display),
	m_PWMOutput (PWM_CLOCK_RATE, PWM_RANGE, true)
{
	config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
	if (backlight_pin)
		m_backlight_pin = backlight_pin;

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
	//TODO move this to a base class
	bool bOK = true;
	if (bOK)
        {
                bOK = m_SPIMaster.Initialize ();
        }

        if (bOK)
        {
                bOK = m_Display.Initialize ();
		//TODO: expose this as a config entry
		m_Display.SetRotation(270);
        }

        if (bOK)
        {
                bOK = m_Graphics.Initialize ();
        }

	// register pages
	LOGNOTE("Registering pages");
	m_PageManager.RegisterPage("splashpage", new ST7789SplashPage(&m_Display, &m_Graphics));
	m_PageManager.RegisterPage("homepage", new ST7789HomePage(&m_Display, &m_Graphics));
	m_PageManager.RegisterPage("imagespage", new ST7789ImagesPage(&m_Display, &m_Graphics));
	m_PageManager.RegisterPage("powerpage", new ST7789PowerPage(&m_Display, &m_Graphics));
	m_PageManager.RegisterPage("configpage", new ST7789ConfigPage(&m_Display, &m_Graphics));
	m_PageManager.RegisterPage("usbconfigpage", new ST7789USBConfigPage(&m_Display, &m_Graphics));

	// Set the stating page
	LOGNOTE("Setting initial page");
	m_PageManager.SetActivePage("splashpage");

	// register buttons
	// TODO: move to base class
	CInterruptSystem* interruptSystem = CInterruptSystem::Get();
	m_GPIOManager = new CGPIOManager(interruptSystem);
	if (bOK) {
		LOGNOTE("GPIO Manager Initializing");
		bOK = m_GPIOManager->Initialize();
	}

	//TODO move to base class
	if (bOK) {
		LOGNOTE("Registering buttons");
		m_ButtonUp = new CGPIOPin(BUTTONUP, GPIOModeInputPullUp, m_GPIOManager);
		static ButtonHandlerContext buttonUpCtx = { this, &m_PageManager, m_ButtonUp, Button::Up };
		m_ButtonUp->ConnectInterrupt (HandleButtonPress, &buttonUpCtx);
		m_ButtonUp->EnableInterrupt (GPIOInterruptOnFallingEdge);

		m_ButtonDown  = new CGPIOPin(BUTTONDOWN, GPIOModeInputPullUp, m_GPIOManager);
		static ButtonHandlerContext buttonDownCtx = { this, &m_PageManager, m_ButtonDown, Button::Down };
		m_ButtonDown->ConnectInterrupt (HandleButtonPress, &buttonDownCtx);
		m_ButtonDown->EnableInterrupt (GPIOInterruptOnFallingEdge);

		m_ButtonOk  = new CGPIOPin(BUTTONOK, GPIOModeInputPullUp, m_GPIOManager);
		static ButtonHandlerContext buttonOkCtx = { this, &m_PageManager, m_ButtonOk, Button::Ok };
		m_ButtonOk->ConnectInterrupt (HandleButtonPress, &buttonOkCtx);
		m_ButtonOk->EnableInterrupt (GPIOInterruptOnFallingEdge);

		m_ButtonCancel  = new CGPIOPin(BUTTONCANCEL, GPIOModeInputPullUp, m_GPIOManager);
		static ButtonHandlerContext buttonCancelCtx = { this, &m_PageManager, m_ButtonCancel, Button::Cancel };
		m_ButtonCancel->ConnectInterrupt (HandleButtonPress, &buttonCancelCtx);
		m_ButtonCancel->EnableInterrupt (GPIOInterruptOnFallingEdge);
	}

	if (bOK) {
		LOGNOTE("Registering backlight");
		m_Backlight = new CGPIOPin(m_backlight_pin, GPIOModeAlternateFunction0, m_GPIOManager);
		m_PWMOutput.Start();
                m_PWMOutput.Write(2, 1024);
		pwm_configured = true;

		// Backlight timeout
		backlightTimeout = config->GetScreenTimeout(DEFAULT_TIMEOUT) * 1000000;
	}

        return bOK;
}

void ST7789Display::Clear() {
    // TODO: Clear screen
}

void ST7789Display::Sleep() {

    if (!pwm_configured)
	    return;

    LOGNOTE("Sleeping");
    sleeping = true;
    m_PWMOutput.Write(2, 32);
}

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

void ST7789Display::Refresh() {

    // Is it time to dim the screen?
    unsigned now = CTimer::Get()->GetClockTicks();
    //LOGNOTE("backlightTimer is %d, now is %d, TIMEOUT is %d", backlightTimer, now, TIMEOUT);
    if (!sleeping && now - backlightTimer > backlightTimeout )
	Sleep();

    m_PageManager.Refresh();
}

bool ST7789Display::Debounce(Button button) {
    unsigned now = CTimer::Get()->GetTicks();
    if (now - lastPressTime[(int)button] < DEBOUNCETICKS) {
        LOGNOTE("Ignored a bounce!");
	return true;
    }

    lastPressTime[(int)button] = now;
    return false;
}

//TODO move to a base class
// This is the callback from the GPIO Button interrupt. We pass this on to the
// page manager to handle
void ST7789Display::HandleButtonPress(void *pParam) {


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
	context->pin->EnableInterrupt (GPIOInterruptOnFallingEdge);
    }
}
