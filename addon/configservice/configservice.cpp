#include "configservice.h"
#include <circle/logger.h>
#include <string.h>
#include <ctype.h>
#include <fatfs/ff.h>
#include "../../src/kernel.h"
#include <circle/logger.h>

LOGMODULE("configservice");

//TODO: Find a way to delete settings

ConfigService *ConfigService::s_pThis = 0;

ConfigService::ConfigService()
{
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    FATFS* fs = CKernel::Get()->GetFileSystem();
    m_properties = new CPropertiesFatFsFile("SD:/config.txt", fs);
    bool ok = m_properties->Load();
    assert(ok && "Can't load configuration properties from config.txt");
    ok = cmdline.Load("SD:/cmdline.txt");
    assert(ok && "Can't load configuration properties from cmdline.txt");
    SetName("configservice");
}

ConfigService::~ConfigService()
{
    delete m_properties;
}

bool ConfigService::GetUSBFullSpeed()
{
    const char* val = cmdline.GetValue("usbspeed");
    if (val != NULL && strcmp(val, "full") == 0)
	    return true;
    return false;
}

void ConfigService::SetUSBFullSpeed(bool value)
{
    cmdline.SetValue("usbspeed", value ? "full" : "high");
    cmdlineIsDirty=true;
}

const char* ConfigService::GetCurrentImage(const char *defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetString("current_image", defaultValue);
}

//TODO bounds checking
unsigned ConfigService::GetDefaultVolume(unsigned defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetNumber("default_volume", defaultValue);
}

//TODO bounds checking
unsigned ConfigService::GetLogLevel(unsigned defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetNumber("loglevel", defaultValue);
}

unsigned ConfigService::GetMode(unsigned defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetNumber("mode", defaultValue);
}

const char* ConfigService::GetLogfile(const char *defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetString("logfile", defaultValue);
}

// TODO enum
const char* ConfigService::GetDisplayHat(const char *defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetString("displayhat", defaultValue);
}

const char* ConfigService::GetTimezone(const char *defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetString("timezone", defaultValue);
}

unsigned ConfigService::GetScreenTimeout(unsigned defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetNumber("screen_timeout", defaultValue);
}


void ConfigService::SetCurrentImage(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("current_image", value);
    configIsDirty=true;
}

void ConfigService::SetMode(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("mode", value);
    configIsDirty=true;
}

void ConfigService::SetDefaultVolume(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("default_volume", value);
    configIsDirty=true;
}

void ConfigService::SetDisplayHat(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("displayhat", value);
    configIsDirty=true;
}

void ConfigService::SetTimezone(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("timezone", value);
    configIsDirty=true;
}

void ConfigService::SetLogLevel(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("loglevel", value);
    configIsDirty=true;
}

void ConfigService::SetLogfile(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("logfile", value);
    configIsDirty=true;
}

void ConfigService::SetScreenTimeout(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("screen_timeout", value);
    configIsDirty=true;
}

bool ConfigService::IsDirty() {
	return configIsDirty || cmdlineIsDirty;
}

bool ConfigService::Save() {
	bool ok = true;
	if (ok && configIsDirty) {
		ok = m_properties->Save();
		configIsDirty = false;
	}

	if (ok && cmdlineIsDirty) {
		ok = cmdline.Save("SD:/cmdline.txt");
		cmdlineIsDirty = false;
	}

	return ok;
}

void ConfigService::Run(void) {
    LOGNOTE("Configservice Run Loop entered");

    while (true) {
            // See if any configuration needs saving. The "Set" method is
            // called from an interrupt and we can't "Save" to disk in an
            // interrupt so we need to do it here
            if (IsDirty()) {
                    LOGNOTE("Saving configuration");
                    Save();
                    LOGNOTE("Saved configuration");
            }

            CScheduler::Get()->MsSleep(20);
    }

}
