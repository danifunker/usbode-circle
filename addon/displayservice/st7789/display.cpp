/*
 / (c) 2025 Ian Cass
 / (c) 2025 Dani Sarfati
 / This is a display driver for USBODE for the ST7789 series screens
 / It's responsible for managing the page rendering
 */
#include "display.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <displayservice/buttonhandler.h>
#include <displayservice/buttons.h>
#include <setupstatus/setupstatus.h>
#include <upgradestatus/upgradestatus.h>

#include "configpage.h"
#include "homepage.h"
#include "imagespage.h"
#include "infopage.h"
#include "logconfigpage.h"
#include "timeoutconfigpage.h"
#include "lowpowertimeoutconfigpage.h"
#include "powerpage.h"
#include "splashpage.h"
#include "usbconfigpage.h"
#include "setuppage.h"
#include "upgradepage.h"
#include "classicmacmodepage.h"
#include "soundconfig.h"
#include "discartpage.h"
#include <scsitbservice/scsitbservice.h>
#include <discart/discart.h>

LOGMODULE("st7789display");

#define SLEEP_WARNING_DURATION 2 * 1000  // 2 seconds in milliseconds
#define LOW_BRIGHTNESS_THRESHOLD 16     // Show warning only if sleep brightness < 16
#define DISCART_DELAY_US 2000000        // 2 seconds delay before showing disc art (in microseconds)

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
      cancel_pin(buttons->Cancel),
      display_rotation(config->display_rotation)
{
    // Obtain our config service
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    // Initialize the backlight variables
    if (config->backlight_pin)
        m_backlight_pin = config->backlight_pin;

    backlightTimer = CTimer::Get()->GetClockTicks();
    lowPowerTimer = backlightTimer;

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
        m_Display.SetRotation(display_rotation);
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
    m_PageManager.RegisterPage("lowpowertimeoutconfigpage", new ST7789LowPowerTimeoutConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("infopage", new ST7789InfoPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("setuppage", new ST7789SetupPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("upgradepage", new ST7789UpgradePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("classicmacmodepage", new ST7789ClassicMacModePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("upgradepage", new ST7789UpgradePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("soundconfigpage", new ST7789SoundConfigPage(&m_Display, &m_Graphics));

    // Register disc art page and keep a reference for direct access
    m_DiscArtPage = new ST7789DiscArtPage(&m_Display, &m_Graphics);
    m_PageManager.RegisterPage("discartpage", m_DiscArtPage);

    LOGNOTE("Registered pages");

    // Set the starting page
    auto setup = SetupStatus::Get();
    auto upgrade = UpgradeStatus::Get();
    if (setup->isSetupRequired()) 
	m_PageManager.SetActivePage("setuppage");
    else if (upgrade->isUpgradeRequired()) 
	m_PageManager.SetActivePage("upgradepage");
    else
    	m_PageManager.SetActivePage("splashpage");

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
        unsigned brightness = configservice->GetST7789Brightness(1024);
        m_PWMOutput.Write(2, brightness);
        pwm_configured = true;
    }

    return bOK;
}

void ST7789Display::Clear() {
    // TODO: Clear screen. Do we need this?
}

void ST7789Display::DrawSleepWarning() {
    // Draw a centered box with "Entering Sleep..." message
    const int boxWidth = 200;
    const int boxHeight = 60;
    const int boxX = (240 - boxWidth) / 2;  // ST7789 is 240x240
    const int boxY = (240 - boxHeight) / 2;
    
    // Draw black filled rectangle with white border
    m_Graphics.DrawRect(boxX, boxY, boxWidth, boxHeight, COLOR2D(255, 255, 255));
    m_Graphics.DrawRect(boxX + 2, boxY + 2, boxWidth - 4, boxHeight - 4, COLOR2D(0, 0, 0));
    
    // Clear the inner area
    for (int y = boxY + 4; y < boxY + boxHeight - 4; y++) {
        for (int x = boxX + 4; x < boxX + boxWidth - 4; x++) {
            m_Graphics.DrawPixel(x, y, COLOR2D(0, 0, 0));
        }
    }
    
    // Draw the text centered in the box
    const char* message = "Entering Sleep...";
    // Center text horizontally and vertically in the box
    m_Graphics.DrawText(boxX + 30, boxY + 25, COLOR2D(255, 255, 255), message, C2DGraphics::AlignLeft, Font8x16);
    m_Graphics.UpdateDisplay();
}

// Enter low power mode - intermediate dimmed state (silent, no warning)
void ST7789Display::EnterLowPower() {
    if (!pwm_configured)
        return;

    // Do not enter low power if we're in the First Boot Setup phase or updating
    if (SetupStatus::Get()->isSetupInProgress() || UpgradeStatus::Get()->isUpgradeInProgress())
        return;

    // If disc art timer has elapsed (m_DiscArtPending), show it before entering low power
    // Only if we're on the homepage (don't interrupt menu navigation)
    bool onHomepage = m_PageManager.GetCurrentPage() == m_PageManager.GetPage("homepage");
    if (onHomepage && m_DiscArtPending) {
        m_DiscArtPending = false;
        if (DiscArt::HasDiscArt(m_LastDiscPath)) {
            LOGNOTE("Showing disc art before low power mode");
            m_DiscArtPage->SetDiscImagePath(m_LastDiscPath);
            m_PageManager.SetActivePage("discartpage");
        }
    }

    LOGNOTE("Entering Low Power Mode");

    lowPowerMode = true;
    lowPowerTimer = CTimer::Get()->GetClockTicks();
    unsigned lowPowerBrightness = configservice->GetST7789LowPowerBrightness(32);
    m_PWMOutput.Write(2, lowPowerBrightness);
}

// Enter sleep mode - final sleep state (with warning if brightness is low)
void ST7789Display::EnterSleep() {
    if (!pwm_configured)
        return;

    // Do not sleep if we're in the First Boot Setup phase or updating
    if (SetupStatus::Get()->isSetupInProgress() || UpgradeStatus::Get()->isUpgradeInProgress())
        return;

    LOGNOTE("Entering Sleep Mode");

    // Check if sleep brightness is low enough to warrant showing warning
    unsigned sleepBrightness = configservice->GetST7789SleepBrightness(0);
    if (sleepBrightness < LOW_BRIGHTNESS_THRESHOLD) {
        DrawSleepWarning();
        CScheduler::Get()->MsSleep(SLEEP_WARNING_DURATION);
        m_PageManager.Refresh(true);
    }

    sleeping = true;
    m_PWMOutput.Write(2, sleepBrightness);
}

// Legacy Sleep() - triggers the appropriate state transition
void ST7789Display::Sleep() {
    if (!lowPowerMode) {
        EnterLowPower();
    } else if (!sleeping) {
        EnterSleep();
    }
}

// Wake the screen
void ST7789Display::Wake() {
    // reset the backlight timer on this keypress
    backlightTimer = CTimer::Get()->GetClockTicks();

    bool wasAsleep = lowPowerMode || sleeping;

    // Wake up if we were in low power or sleeping
    if (wasAsleep) {
        unsigned brightness = configservice->GetST7789Brightness(1024);
        m_PWMOutput.Write(2, brightness);

        // Just refresh the current page - don't change screens on wake
        m_PageManager.Refresh(true);
    }

    // Regardless, we're definitely not in low power or sleeping now
    lowPowerMode = false;
    sleeping = false;
}

bool ST7789Display::IsSleeping() {
    return sleeping;
}

// Called by displaymanager kernel loop. Check backlight timeout and sleep if
// necessary. Pass on the refresh call to the page manager
void ST7789Display::Refresh() {
    unsigned lowPowerTimeout = configservice->GetLowPowerTimeout(15) * 1000000;
    unsigned sleepTimeout = configservice->GetScreenTimeout(DEFAULT_TIMEOUT) * 1000000;
    unsigned now = CTimer::Get()->GetClockTicks();

    // If we're asleep or in low power and both timeouts got changed to zero, wake up
    if (!lowPowerTimeout && !sleepTimeout && (sleeping || lowPowerMode))
        Wake();

    // State machine: Active -> Low Power -> Sleep
    if (!sleeping && !lowPowerMode) {
        // In active state - check for low power timeout
        if (lowPowerTimeout) {
            if (now - backlightTimer > lowPowerTimeout)
                EnterLowPower();
        }
    } else if (lowPowerMode && !sleeping) {
        // In low power state - check for sleep timeout
        if (sleepTimeout) {
            if (now - lowPowerTimer > sleepTimeout)
                EnterSleep();
        }
    }

    // Check for disc art display timer
    CheckDiscArtTimer();

    m_PageManager.Refresh();
}

// Check if a disc was recently loaded and show disc art after delay
void ST7789Display::CheckDiscArtTimer() {
    // Don't do disc art processing during low power or sleep modes
    if (lowPowerMode || sleeping) {
        return;
    }

    // Get current disc path
    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) return;

    const char* currentPath = svc->GetCurrentCDPath();
    if (!currentPath || currentPath[0] == '\0') {
        m_DiscArtPending = false;
        m_LastDiscPath[0] = '\0';
        return;
    }

    // Check if we're on homepage (only show disc art from homepage)
    bool onHomepage = m_PageManager.GetCurrentPage() == m_PageManager.GetPage("homepage");

    // Check if disc changed
    if (strcmp(m_LastDiscPath, currentPath) != 0) {
        strncpy(m_LastDiscPath, currentPath, sizeof(m_LastDiscPath) - 1);
        m_LastDiscPath[sizeof(m_LastDiscPath) - 1] = '\0';

        // Only start the disc art timer if we're on homepage
        // This prevents the timer from running during splash/other screens
        if (onHomepage) {
            m_DiscLoadTime = CTimer::Get()->GetClockTicks();
            m_DiscArtPending = true;
        }

        // Update the disc art page with new path
        if (m_DiscArtPage) {
            m_DiscArtPage->SetDiscImagePath(currentPath);
        }

        // Don't check timer this cycle - wait for next refresh
        return;
    }

    // If we just arrived at homepage and have a disc but timer not started yet, start it now
    if (onHomepage && !m_DiscArtPending && m_LastDiscPath[0] != '\0' && m_DiscLoadTime == 0) {
        m_DiscLoadTime = CTimer::Get()->GetClockTicks();
        m_DiscArtPending = true;
        return;
    }

    // Check if it's time to show disc art (already confirmed not sleeping/low power above)
    if (m_DiscArtPending && onHomepage) {
        unsigned now = CTimer::Get()->GetClockTicks();

        if (now - m_DiscLoadTime > DISCART_DELAY_US) {
            m_DiscArtPending = false;
            m_DiscLoadTime = 0;  // Reset for next disc change
            ShowDiscArt();
        }
    }
}

// Show disc art page if art is available
void ST7789Display::ShowDiscArt() {
    if (!m_DiscArtPage) return;

    // Check if we have disc art for current disc
    if (DiscArt::HasDiscArt(m_LastDiscPath)) {
        m_DiscArtPage->SetDiscImagePath(m_LastDiscPath);
        m_PageManager.SetActivePage("discartpage");
    }
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
    //LOGNOTE("Got button press %d", context->button);
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
