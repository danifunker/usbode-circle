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
    if (val != nullptr && strcmp(val, "full") == 0) {
	return true;
    }
    return false;
}

void ConfigService::SetUSBFullSpeed(bool value)
{
    cmdline.SetValue("usbspeed", value ? "full" : "high");
    cmdlineIsDirty=true;
}

void ConfigService::SetSoundDev(const char* value)
{
    cmdline.SetValue("sounddev", value);
    cmdlineIsDirty=true;
}

const char* ConfigService::GetSoundDev(const char* defaultValue)
{
    const char* val = cmdline.GetValue("sounddev");
    if (val != nullptr )
	    return val;
    return defaultValue;
}

const char* ConfigService::GetCurrentImage(const char *defaultValue)
{
    return GetProperty("current_image", defaultValue);
}

//TODO bounds checking
unsigned ConfigService::GetDefaultVolume(unsigned defaultValue)
{
    return GetProperty("default_volume", defaultValue);
}

//TODO bounds checking
unsigned ConfigService::GetLogLevel(unsigned defaultValue)
{
    const char* val = cmdline.GetValue("loglevel");
    if (val == nullptr )
	    return defaultValue;

    int parsed = atoi(val);
    if (parsed < 0)
	    return defaultValue;

    return static_cast<unsigned>(parsed);
}

unsigned ConfigService::GetMode(unsigned defaultValue)
{
    return GetProperty("mode", defaultValue);
}

const char* ConfigService::GetLogfile(const char *defaultValue)
{
    return GetProperty("logfile", defaultValue);
}

// TODO enum
const char* ConfigService::GetDisplayHat(const char *defaultValue)
{
    return GetProperty("displayhat", defaultValue);
}

const char* ConfigService::GetTimezone(const char *defaultValue)
{
    return GetProperty("timezone", defaultValue);
}

unsigned ConfigService::GetScreenTimeout(unsigned defaultValue)
{
    return GetProperty("screen_timeout", defaultValue);
}

unsigned ConfigService::GetST7789Brightness(unsigned defaultValue)
{
    return GetProperty("st7789_brightness", defaultValue);
}

unsigned ConfigService::GetST7789SleepBrightness(unsigned defaultValue)
{
    return GetProperty("st7789_sleep_brightness", defaultValue);
}

void ConfigService::SetCurrentImage(const char* value)
{
    SetProperty("current_image", value);
}

void ConfigService::SetMode(unsigned value)
{
    SetProperty("mode", value);
}

void ConfigService::SetDefaultVolume(unsigned value)
{
    SetProperty("default_volume", value);
}

void ConfigService::SetDisplayHat(const char* value)
{
    SetProperty("displayhat", value);
}

void ConfigService::SetTimezone(const char* value)
{
    SetProperty("timezone", value);
}

void ConfigService::SetLogLevel(unsigned value)
{
    char buffer[2];
    snprintf(buffer, sizeof(buffer), "%u", value);
    cmdline.SetValue("loglevel", buffer);
    cmdlineIsDirty=true;
}

void ConfigService::SetLogfile(const char* value)
{
    SetProperty("logfile", value);
}

void ConfigService::SetScreenTimeout(unsigned value)
{
    SetProperty("screen_timeout", value);
}

void ConfigService::SetST7789Brightness(unsigned value)
{
    SetProperty("st7789_brightness", value);
}

void ConfigService::SetST7789SleepBrightness(unsigned value)
{
    SetProperty("st7789_sleep_brightness", value);
}

void ConfigService::SetProperty(const char* key, const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString(key, value);
    configIsDirty = true;
}

void ConfigService::SetProperty(const char* key, unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber(key, value);
    configIsDirty = true;
}

const char* ConfigService::GetProperty(const char* key, const char* defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetString(key, defaultValue);
}

unsigned ConfigService::GetProperty(const char* key, unsigned defaultValue)
{
    m_properties->SelectSection("usbode");
    return m_properties->GetNumber(key, defaultValue);
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
