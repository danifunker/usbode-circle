/*
/ (c) 2025 Ian Cass
/ (c) 2025 Dani Sarfati
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
#include "sh1106/display.h"

#define CONFIG_FILE "SD:/config.txt"

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

	DisplayConfig config = {
	    .dc_pin = 9,
	    .reset_pin = 27,
	    .backlight_pin = 13,
	    .spi_cpol = 0,
	    .spi_cpha = 0,
	    .spi_clock_speed = 80000000,
	    .spi_chip_select = 1
	};

	ButtonConfig buttons = {
		.Up = ST7789_BUTTONUP,
		.Down = ST7789_BUTTONDOWN,
		.Ok = ST7789_BUTTONOK,
		.Cancel = ST7789_BUTTONCANCEL
	};

        m_IDisplay = new ST7789Display(&config, &buttons);

    // Generic ST7789 screen depends on how you wired it up. The default values
    // (mostly) mirror the wiring of the Pirate Audio screen
    // https://pinout.xyz/pinout/pirate_audio_line_out
    } else if (strcmp(displayType, "st7789") == 0) {
        //TODO: Bring this into configservice
        FATFS* fs = CKernel::Get()->GetFileSystem();
        CPropertiesFatFsFile* properties = new CPropertiesFatFsFile(CONFIG_FILE, fs);
	properties->Load();
        properties->SelectSection("st7789");

	DisplayConfig config = {
            .dc_pin = properties->GetNumber("dc_pin", 22),
            .reset_pin = properties->GetNumber("reset_pin", 27),
            .backlight_pin = properties->GetNumber("backlight_pin", 13),
            .spi_cpol = properties->GetNumber("spi_cpol", 1),
            .spi_cpha = properties->GetNumber("spi_chpa", 1),
            .spi_clock_speed = properties->GetNumber("spi_clock_speed", 80000000),
            .spi_chip_select = properties->GetNumber("spi_chip_select", 0)
	};

	ButtonConfig buttons = {
		.Up = properties->GetNumber("button_up", ST7789_BUTTONUP),
		.Down = properties->GetNumber("button_down", ST7789_BUTTONDOWN),
		.Ok = properties->GetNumber("button_ok", ST7789_BUTTONOK),
		.Cancel = properties->GetNumber("button_cancel", ST7789_BUTTONCANCEL)
	};

        m_IDisplay = new ST7789Display(&config, &buttons);

    // Generic SH1106 screen depends on how you wired it up. The default values
    // (mostly) mirror the wiring of the Pirate Audio screen
    // https://pinout.xyz/pinout/pirate_audio_line_out
    } else if (strcmp(displayType, "sh1106") == 0) {
        FATFS* fs = CKernel::Get()->GetFileSystem();
        CPropertiesFatFsFile* properties = new CPropertiesFatFsFile(CONFIG_FILE, fs);
	properties->Load();
        properties->SelectSection("sh1106");

	DisplayConfig config = {
            .dc_pin = properties->GetNumber("dc_pin", 22),
            .reset_pin = properties->GetNumber("reset_pin", 27),
            .backlight_pin = properties->GetNumber("backlight_pin", 0),
            .spi_cpol = properties->GetNumber("spi_cpol", 0),
            .spi_cpha = properties->GetNumber("spi_chpa", 0),
            .spi_clock_speed = properties->GetNumber("spi_clock_speed", 24000000),
            .spi_chip_select = properties->GetNumber("spi_chip_select", 1)
	};

	// Default to bare minimum button config
        ButtonConfig buttons = {
            .Up = properties->GetNumber("button_up", SH1106_BUTTONUP),
            .Down = properties->GetNumber("button_down", SH1106_BUTTONDOWN),
            .Left = properties->GetNumber("button_left", 0),
            .Right = properties->GetNumber("button_right", 0),
            .Ok = properties->GetNumber("button_ok", SH1106_BUTTONOK),
            .Cancel = properties->GetNumber("button_cancel", SH1106_BUTTONCANCEL),
            .Key3 = properties->GetNumber("button_key3", 0),
            .Center = properties->GetNumber("button_center", 0)
	};

        m_IDisplay = new SH1106Display(&config, &buttons);
    } else if (strcmp(displayType, "waveshare") == 0) {
	DisplayConfig config = {
            .dc_pin = 24,
            .reset_pin = 25,
            .backlight_pin = 0,
            .spi_cpol = 0,
            .spi_cpha = 0,
            .spi_clock_speed = 24000000,
            .spi_chip_select = 0
	};

    ButtonConfig buttons = {
        .Up = SH1106_BUTTONUP,
        .Down = SH1106_BUTTONDOWN,
        .Left = SH1106_BUTTONLEFT,
        .Right = SH1106_BUTTONRIGHT,
        .Ok = SH1106_BUTTONOK,
        .Cancel = SH1106_BUTTONCANCEL,
        .Key3 = SH1106_BUTTONKEY3,
        .Center = SH1106_BUTTONCENTER
	};

        m_IDisplay = new SH1106Display(&config, &buttons);
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
	if (m_IDisplay)
            m_IDisplay->Refresh();

        CScheduler::Get()->MsSleep(50);  // tick rate for page changes
    }
}
