
/*
/ (c) 2025
/ Display driver for USBODE for MT32-Pi-compatible HATs (I2C SSD1306 OLED).
/ Responsible for managing page rendering and the four tactile buttons.
/ Mirrors the SH1106 driver; the only hardware difference is I2C transport
/ via CSSD1306GfxDisplay instead of SPI.
*/
#include "display.h"

#include <circle/logger.h>
#include <circle/machineinfo.h>
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
#include "classicmacmodepage.h"
#include "soundconfig.h"

LOGMODULE("mt32pidisplay");

MT32PiDisplay::MT32PiDisplay(SSD1306Config* config, ButtonConfig* buttons)
    : m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), FALSE),
      m_Display(&m_I2CMaster, config->i2c_address, config->oled_width, config->oled_height),
      m_Graphics(&m_Display),
      up_pin(buttons->Up),
      down_pin(buttons->Down),
      ok_pin(buttons->Ok),
      cancel_pin(buttons->Cancel),
      display_rotation(config->display_rotation)
{
    // Obtain our config service
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    backlightTimer = CTimer::Get()->GetClockTicks();

    LOGNOTE("Started MT32Pi (SSD1306) Display");
}

// Destructor
MT32PiDisplay::~MT32PiDisplay() {
    delete m_ButtonUp;
    delete m_ButtonDown;
    delete m_ButtonOk;
    delete m_ButtonCancel;
    delete m_GPIOManager;

    LOGNOTE("MT32PiDisplay resources released.");
}

bool MT32PiDisplay::Initialize() {
    bool bOK = true;

    if (bOK) {
        bOK = m_I2CMaster.Initialize();
        LOGNOTE("Initialized I2C");
    }

    if (bOK) {
        bOK = m_Display.Initialize();
        // 180-degree rotation flips the panel's segment/COM scan order. Other
        // angles are not supported on this 1bpp panel.
        m_Display.SetRotation(display_rotation == 180);
        LOGNOTE("Initialized SSD1306 Display");
    }

    if (bOK) {
        bOK = m_Graphics.Initialize();
        LOGNOTE("Initialized Graphics");
    }

    // register pages
    m_PageManager.RegisterPage("homepage", new SSD1306HomePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("imagespage", new SSD1306ImagesPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("powerpage", new SSD1306PowerPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("configpage", new SSD1306ConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("usbconfigpage", new SSD1306USBConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("logconfigpage", new SSD1306LogConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("timeoutconfigpage", new SSD1306TimeoutConfigPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("infopage", new SSD1306InfoPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("setuppage", new SSD1306SetupPage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("upgradepage", new SSD1306UpgradePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("classicmacmodepage", new SSD1306ClassicMacModePage(&m_Display, &m_Graphics));
    m_PageManager.RegisterPage("soundconfigpage", new SSD1306SoundConfigPage(&m_Display, &m_Graphics));

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

        LOGNOTE("Registered buttons");
    }

    return bOK;
}

void MT32PiDisplay::Clear() {
    // TODO: Clear screen. Do we need this?
}

// Dim the screen or even turn it off
void MT32PiDisplay::Sleep() {
    // Do not sleep if we're in the First Boot Setup phase
    if ((SetupStatus::Get() && SetupStatus::Get()->isSetupInProgress()) || UpgradeStatus::Get()->isUpgradeInProgress())
        return;

    LOGNOTE("Sleep warning for %d ms", MT32PI_SLEEP_WARNING_DURATION);
    DrawSleepWarning();
    CScheduler::Get()->MsSleep(MT32PI_SLEEP_WARNING_DURATION);
    LOGNOTE("Sleeping");
    m_PageManager.Refresh(true);
    sleeping = true;
    m_Display.Off();
}

// Wake the screen
void MT32PiDisplay::Wake() {
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

bool MT32PiDisplay::IsSleeping() {
    return sleeping;
}

void MT32PiDisplay::DrawSleepWarning() {
    // Draw a centered box with "Entering Sleep..." message
    const int boxWidth = 110;
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
void MT32PiDisplay::Refresh() {
    // Handle any button presses queued by the interrupt handler. Done here, in
    // task context, so all display I2C stays off the IRQ path (see
    // HandleButtonPress).
    ProcessPendingInput();

    unsigned backlightTimeout = configservice->GetScreenTimeout(MT32PI_DEFAULT_TIMEOUT) * 1000000;

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
bool MT32PiDisplay::Debounce(Button button) {
    unsigned now = CTimer::Get()->GetTicks();
    if (now - lastPressTime[(int)button] < MT32PI_DEBOUNCETICKS) {
        LOGNOTE("Ignored a bounce!");
        return true;
    }

    lastPressTime[(int)button] = now;
    return false;
}

// This is the callback from the GPIO Button interrupt. It runs in IRQ context,
// so it must NOT touch the display: the SSD1306 is driven over I2C, whose Circle
// driver takes a TASK_LEVEL spinlock and shares the bus with the CDPlayer DAC.
// Doing I2C here can deadlock against an in-flight task-level transaction. We
// only debounce (pure timer math) and flag the press; ProcessPendingInput(),
// called from the task-level Refresh() loop, does the wake and page handling.
void MT32PiDisplay::HandleButtonPress(void* pParam) {
    ButtonHandlerContext* context = static_cast<ButtonHandlerContext*>(pParam);
    if (context) {
        MT32PiDisplay* self = static_cast<MT32PiDisplay*>(context->display);

        if (self->Debounce(context->button))
            return;

        // Queue the press for task-context handling.
        self->m_PendingButton[static_cast<int>(context->button)] = true;
    }
}

// Runs in task context (from Refresh()). Safe to perform display I2C here.
void MT32PiDisplay::ProcessPendingInput() {
    for (int i = 0; i < static_cast<int>(Button::Count); i++) {
        if (!m_PendingButton[i])
            continue;
        m_PendingButton[i] = false;

        Button button = static_cast<Button>(i);
        LOGNOTE("Handling button press %d", i);

        bool wasSleeping = IsSleeping();
        Wake();

        // If it was sleeping, the keypress only wakes the screen.
        if (wasSleeping)
            continue;

        if (UpgradeStatus::Get()->isUpgradeInProgress()) {
            LOGNOTE("Button press ignored - upgrade in progress");
            continue;
        }

        m_PageManager.HandleButtonPress(button);
    }
}
