#include "displayservice.h"

#include <assert.h>
#include <circle/sched/scheduler.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/logger.h>
#include <Properties/propertiesfatfsfile.h>
#include "st7789/display.h"
#include "../../src/kernel.h"

#define CONFIG_FILE "config.txt"

LOGMODULE("displayservice");

DisplayService *DisplayService::s_pThis = 0;

DisplayService::DisplayService(const char* displayType)
{

    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    LOGNOTE("Display Service starting");
    SetName("displayservice");

    CreateDisplay(displayType);

    boolean ok = Initialize();
    assert(ok == true);
}

void DisplayService::CreateDisplay(const char* displayType) {
    // Create the appropriate display
    assert(displayType != nullptr && "displayType cannot be nullptr!");
    assert(strcmp(displayType, "none") != 0  && "displayType cannot be \"none\"!");

    if (strcmp(displayType, "pirateaudiolineout") == 0) {
        m_IDisplay = new ST7789Display(
		9, // dc_pin
		27, // reset_pin
		13, // backlight_pin
		0, // spi_cpol
		0, // spi_chpa
		80000000, // spi_clock_speed
		1 // spi_chip_select
	);
    } else if (strcmp(displayType, "st7789") == 0) {

	 FATFS* fs = CKernel::Get()->GetFileSystem();
         CPropertiesFatFsFile* properties = new CPropertiesFatFsFile(CONFIG_FILE, fs);
	 properties->SelectSection("st7789");
	 
	//TODO: Get these values from properties
	//TODO: but first implement a configuration service
        m_IDisplay = new ST7789Display(
		properties->GetNumber("dc_pin", 22), // dc_pin
		properties->GetNumber("reset_pin", 27), // reset_pin
		properties->GetNumber("backlight_pin", 13), // backlight_pin
		properties->GetNumber("spi_cpol", 1), // spi_cpol
		properties->GetNumber("spi_chpa", 1), // spi_chpa
		properties->GetNumber("spi_clock_speed", 80000000), // spi_clock_speed
		properties->GetNumber("spi_chip_select", 0) // spi_chip_select
	);
    }
    assert(m_IDisplay != nullptr && "Didn't create display");
}

boolean DisplayService::Initialize() {
    bool bOK = true;
    if (bOK) {
    	LOGNOTE("Display Service Initializing");
	bOK = m_IDisplay->Initialize();
    }

    return bOK;
}

DisplayService::~DisplayService(void) {
    s_pThis = 0;
    delete m_IDisplay;
}

void DisplayService::Run(void) {
    LOGNOTE("Display Run Loop entered");

    while (true) {
	    m_IDisplay->Refresh();
	    CScheduler::Get()->MsSleep(20); //tick rate for page changes
    }

}
