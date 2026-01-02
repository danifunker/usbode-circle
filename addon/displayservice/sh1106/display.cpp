
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
#include <setupstatus/setupstatus.h>
#include "configpage.h"
#include "homepage.h"
#include "imagespage.h"
#include "infopage.h"
#include "logconfigpage.h"
#include "timeoutconfigpage.h"
#include "powerpage.h"
#include "usbconfigpage.h"
#include "setuppage.h"
#include "upgradepage.h"
#include <upgradestatus/upgradestatus.h>

LOGMODULE("sh1106display");

SH1106Display::SH1106Display(DisplayConfig* config, ButtonConfig* buttons)
    : m_SPIMaster(config->spi_clock_speed, config->spi_cpol, config->spi_cpha, 0),
      m_Display(&m_SPIMaster, config->dc_pin, config->reset_pin, 128, 64,
		config->spi_clock_speed, config->spi_cpol, config->spi_cpha, config->spi_chip_select),
      m_Graphics(&m_Display),
      up_pin(buttons->Up),
      down_pin(buttons->Down),
      ok_pin(buttons->Ok),
      cancel_pin(buttons->Cancel),
      left_pin(buttons->Left),
      right_pin(buttons->Right),
      center_pin(buttons->Center),
      key3_pin(buttons->Key3),
      display_rotation(config->display_rotation)
{

    // Obtain our config service
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    backlightTimer = CTimer::Get()->GetClockTicks();

    LOGNOTE("Started SH1106Display Display");
}

// Destructor
SH1106Display::~SH1106Display() {
    delete m_ButtonUp;
    delete m_ButtonDown;
    delete m_ButtonOk;
    delete m_ButtonCancel;
    delete m_ButtonLeft;
    delete m_ButtonRight;
    delete m_ButtonCenter;
    delete m_ButtonKey3;
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
        m_Display.InvertDisplay(display_rotation == 180);
        LOGNOTE("Initialized SH1106 Display");
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
    m_PageManager.RegisterPage("timeoutconfigpage", new SH1106TimeoutConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("infopage", new SH1106InfoPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("setuppage", new SH1106SetupPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("upgradepage", new SH1106UpgradePage(&m_Display, &m_Graphics));

    // Set the starting page
    SetupStatus* setup = SetupStatus::Get();
    UpgradeStatus* upgrade = UpgradeStatus::Get();

    if (setup->isSetupRequired())
        m_PageManager.SetActivePage("setuppage");
    else if (upgrade->isUpgradeRequired()) 
        m_PageManager.SetActivePage("upgradepage");
    else
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

	if (left_pin) {
            m_ButtonLeft = new CGPIOPin(left_pin, GPIOModeInputPullUp, m_GPIOManager);
            static ButtonHandlerContext buttonLeftCtx = {this, &m_PageManager, m_ButtonLeft, Button::Left};
            m_ButtonLeft->ConnectInterrupt(HandleButtonPress, &buttonLeftCtx);
            m_ButtonLeft->EnableInterrupt(GPIOInterruptOnFallingEdge);
	}

	if (right_pin) {
            m_ButtonRight = new CGPIOPin(right_pin, GPIOModeInputPullUp, m_GPIOManager);
            static ButtonHandlerContext buttonRightCtx = {this, &m_PageManager, m_ButtonRight, Button::Right};
            m_ButtonRight->ConnectInterrupt(HandleButtonPress, &buttonRightCtx);
            m_ButtonRight->EnableInterrupt(GPIOInterruptOnFallingEdge);
	}

	if (center_pin) {
            m_ButtonCenter = new CGPIOPin(center_pin, GPIOModeInputPullUp, m_GPIOManager);
            static ButtonHandlerContext buttonCenterCtx = {this, &m_PageManager, m_ButtonCenter, Button::Center};
            m_ButtonCenter->ConnectInterrupt(HandleButtonPress, &buttonCenterCtx);
            m_ButtonCenter->EnableInterrupt(GPIOInterruptOnFallingEdge);
	}

	if (key3_pin) {
            m_ButtonKey3 = new CGPIOPin(key3_pin, GPIOModeInputPullUp, m_GPIOManager);
            static ButtonHandlerContext buttonKey3Ctx = {this, &m_PageManager, m_ButtonKey3, Button::Key3};
            m_ButtonKey3->ConnectInterrupt(HandleButtonPress, &buttonKey3Ctx);
            m_ButtonKey3->EnableInterrupt(GPIOInterruptOnFallingEdge);
	}

        LOGNOTE("Registered buttons");
    }

    return bOK;
}

void SH1106Display::Clear() {
    // TODO: Clear screen. Do we need this?
}

// Dim the screen or even turn it off
void SH1106Display::Sleep() {
    // Do not sleep if we're in the First Boot Setup phase
    if (SetupStatus::Get() && SetupStatus::Get()->isSetupInProgress() || UpgradeStatus::Get()->isUpgradeInProgress() )
        return;

    LOGNOTE("Sleep warning for %d ms", SLEEP_WARNING_DURATION);
    DrawSleepWarning();
    CScheduler::Get()->MsSleep(SLEEP_WARNING_DURATION);
    LOGNOTE("Sleeping");
    m_PageManager.Refresh(true);
    sleeping = true;
    m_Display.Off();
}

// Wake the screen
void SH1106Display::Wake() {
    // reset the backlight timer on this keypress
    backlightTimer = CTimer::Get()->GetClockTicks();

    // Wake up if we were sleeping
    if (sleeping) {
        m_Display.On();
        LOGNOTE("Waking");
	m_PageManager.Refresh(true);
    }

    // Regardless, we're definitely not sleeping now
    sleeping = false;
}

bool SH1106Display::IsSleeping() {
    return sleeping;
}

void SH1106Display::DrawSleepWarning() {
    // Draw a centered box with "Entering Sleep..." message
    const int boxWidth = 110;  // Increased from 100 to accommodate text
    const int boxHeight = 24;
    const int boxX = (m_Display.GetWidth() - boxWidth) / 2;
    const int boxY = (m_Display.GetHeight() - boxHeight) / 2;
    
    // Draw black filled rectangle with white border
    m_Graphics.DrawRect(boxX, boxY, boxWidth, boxHeight, COLOR2D(255, 255, 255));
    m_Graphics.DrawRect(boxX + 1, boxY + 1, boxWidth - 2, boxHeight - 2, COLOR2D(0, 0, 0));
    
    // Clear the inner area
    for (int y = boxY + 2; y < boxY + boxHeight - 2; y++) {
        for (int x = boxX + 2; x < boxX + boxWidth - 2; x++) {
            m_Graphics.DrawPixel(x, y, COLOR2D(0, 0, 0));
        }
    }
    
    // Draw the text centered in the box
    const char* message = "Entering Sleep...";
    m_Graphics.DrawText(boxX + 5, boxY + 9, COLOR2D(255, 255, 255), message, C2DGraphics::AlignLeft, Font6x7);
    
    m_Graphics.UpdateDisplay();
}

// Called by displaymanager kernel loop. Check backlight timeout and sleep if
// necessary. Pass on the refresh call to the page manager
void SH1106Display::Refresh() {
    unsigned backlightTimeout = configservice->GetScreenTimeout(DEFAULT_TIMEOUT) * 1000000;

    // If we're asleep and the timeout got changed to zero
    if (!backlightTimeout && sleeping)
            Wake();

    if (!sleeping) {
        if (backlightTimeout) {
            // Is it time to sleep?
            unsigned now = CTimer::Get()->GetClockTicks();
            if (now - backlightTimer > backlightTimeout)
                Sleep();
        }
    }
    
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

        UpgradeStatus* upgrade = UpgradeStatus::Get();
        if (upgrade->isUpgradeInProgress()) {
            LOGNOTE("Button press ignored - upgrade in progress");
            context->pin->EnableInterrupt(GPIOInterruptOnFallingEdge);
            return;
        }

        context->pin->DisableInterrupt();
        context->pageManager->HandleButtonPress(context->button);
        context->pin->EnableInterrupt(GPIOInterruptOnFallingEdge);
    }
}
