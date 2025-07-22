/*
/ (c) 2025 Ian Cass
/ This is a Circle CTask service which displays the UI for USBODE
*/

#include "displayservice.h"

#include <Properties/propertiesfatfsfile.h>
#include <assert.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <configservice/configservice.h>

#include "../../src/kernel.h"
#include "st7789/display.h"

#define CONFIG_FILE "config.txt"

LOGMODULE("displayservice");

DisplayService* DisplayService::s_pThis = 0;

DisplayService::DisplayService(const char* displayType) {
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    LOGNOTE("Display Service starting");
    SetName("displayservice");

    CreateDisplay(displayType);

    boolean ok = Initialize();
    assert(ok == true);
}

// Create the initial display driver. This is called from the constructor
void DisplayService::CreateDisplay(const char* displayType) {
    // It's assumed that the service will only get called by kernel if
    // the display type is acceptable

    assert(displayType != nullptr && "displayType cannot be nullptr!");
    assert(strcmp(displayType, "none") != 0 && "displayType cannot be \"none\"!");

    // Pirate Audio Screen has hard coded values
    if (strcmp(displayType, "pirateaudiolineout") == 0) {
        m_IDisplay = new ST7789Display(
            9,         // dc_pin
            27,        // reset_pin
            13,        // backlight_pin
            0,         // spi_cpol
            0,         // spi_chpa
            80000000,  // spi_clock_speed
            1          // spi_chip_select
        );

    // Generic ST7789 screen depends on how you wired it up. The default values
    // mirror the wiring of the Pirate Audio screen
    // https://pinout.xyz/pinout/pirate_audio_line_out
    } else if (strcmp(displayType, "st7789") == 0) {
        //TODO: Bring this into configservice
        FATFS* fs = CKernel::Get()->GetFileSystem();
        CPropertiesFatFsFile* properties = new CPropertiesFatFsFile(CONFIG_FILE, fs);
        properties->SelectSection("st7789");
        m_IDisplay = new ST7789Display(
            properties->GetNumber("dc_pin", 22),                 // dc_pin
            properties->GetNumber("reset_pin", 27),              // reset_pin
            properties->GetNumber("backlight_pin", 13),          // backlight_pin
            properties->GetNumber("spi_cpol", 1),                // spi_cpol
            properties->GetNumber("spi_chpa", 1),                // spi_chpa
            properties->GetNumber("spi_clock_speed", 80000000),  // spi_clock_speed
            properties->GetNumber("spi_chip_select", 0)          // spi_chip_select
        );
    }
    assert(m_IDisplay != nullptr && "Didn't create display");
}

// Initialize the display. This is called from the constructor
boolean DisplayService::Initialize() {
    bool bOK = true;
    if (bOK) {
        LOGNOTE("Display Service Initializing");
        bOK = m_IDisplay->Initialize();
    }

    return bOK;
}

// Destructor
DisplayService::~DisplayService(void) {
    s_pThis = 0;
    delete m_IDisplay;
}

// The run loop. The primary purpose here is to call the refresh method
// on the display at a regular interval. This is for screen updates and
// menu transitions.
void DisplayService::Run(void) {
    LOGNOTE("Display Run Loop entered");

    while (true) {
        // Refresh our display
        m_IDisplay->Refresh();

        CScheduler::Get()->MsSleep(20);  // tick rate for page changes
    }
}
