#include "configservice.h"
#include "config.h"
#include "cmdline.h"

#include <assert.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include "simpleini.hpp"

LOGMODULE("configservice");

ConfigService *ConfigService::s_pThis = nullptr;

const char* ConfigService::USBTargetOSToString(USBTargetOS os)
{
    switch (os) {
        case USBTargetOS::DosWin:
            return "doswin";
        case USBTargetOS::Apple:
            return "apple";
        default:
            return "doswin";
    }
}

USBTargetOS ConfigService::StringToUSBTargetOS(const char* str)
{
    if (str == nullptr)
        return USBTargetOS::DosWin;
    
    if (strcmp(str, "apple") == 0)
        return USBTargetOS::Apple;
    else if (strcmp(str, "doswin") == 0)
        return USBTargetOS::DosWin;
    
    return USBTargetOS::DosWin;
}

ConfigService::ConfigService() 
:  m_cmdline(new CmdLine()),
 m_config(new Config())
{
    // I am the one and only!
    assert(s_pThis == nullptr);
    s_pThis = this;

    bool ok = m_cmdline->Load(CMDLINE_FILE);
    assert(ok && "Can't load configuration properties from cmdline.txt");

    ok = m_config->Load(CONFIG_FILE);
    assert(ok && "Can't load configuration properties from config.txt");

    SetName("configservice");
}

ConfigService::~ConfigService()
{
    delete m_config;
    delete m_cmdline;
}

bool ConfigService::GetUSBFullSpeed()
{
    const char* val = m_cmdline->GetValue("usbspeed");
    if (val != nullptr && strcmp(val, "full") == 0) {
	return true;
    }
    return false;
}

void ConfigService::SetUSBFullSpeed(bool value)
{
    m_cmdline->SetValue("usbspeed", value ? "full" : "high");
}

void ConfigService::SetSoundDev(const char* value)
{
    if (value != nullptr && strcmp(value, "none") == 0) {
        m_cmdline->DeleteValue("sounddev");
    } else {
        m_cmdline->SetValue("sounddev", value);
    }
}
USBTargetOS ConfigService::GetUSBTargetOS(USBTargetOS defaultValue)
{
    const char* str = m_config->GetString("usbtargetos", USBTargetOSToString(defaultValue));
    return StringToUSBTargetOS(str);
}

void ConfigService::SetUSBTargetOS(USBTargetOS value)
{
    m_config->SetString("usbtargetos", USBTargetOSToString(value));
}

const char* ConfigService::GetSoundDev(const char* defaultValue)
{
    const char* val = m_cmdline->GetValue("sounddev");
    if (val != nullptr )
	    return val;
    return defaultValue;
}

const char* ConfigService::GetTheme(const char* defaultValue)
{
    return m_config->GetString("theme", defaultValue);
}

bool ConfigService::GetFlatFileList(bool defaultValue)
{
    return m_config->GetNumber("flat_file_list", defaultValue ? 1 : 0) != 0;
}

void ConfigService::SetFlatFileList(bool value)
{
    m_config->SetNumber("flat_file_list", value ? 1 : 0);
}

const char* ConfigService::GetCurrentImage(const char* defaultValue)
{
    return m_config->GetString("current_image", defaultValue);
}

u16 ConfigService::GetUSBCDRomVendorId(u16 defaultValue)
{
    return (u16)m_config->GetNumber("usbcdrom_vid", defaultValue);
}

u16 ConfigService::GetUSBCDRomProductId(u16 defaultValue)
{
    return (u16)m_config->GetNumber("usbcdrom_pid", defaultValue);
}

void ConfigService::SetUSBCDRomVendorId(u16 value)
{
    m_config->SetNumber("usbcdrom_vid", value);
}

void ConfigService::SetUSBCDRomProductId(u16 value)
{
    m_config->SetNumber("usbcdrom_pid", value);
}

unsigned ConfigService::GetDefaultVolume(unsigned defaultValue)
{
    return m_config->GetNumber("default_volume", defaultValue);
}

//TODO bounds checking
unsigned ConfigService::GetLogLevel(unsigned defaultValue)
{
    const char* val = m_cmdline->GetValue("loglevel");
    if (val == nullptr )
	    return defaultValue;

    int parsed = atoi(val);
    if (parsed < 0)
	    return defaultValue;

    return static_cast<unsigned>(parsed);
}

unsigned ConfigService::GetMode(unsigned defaultValue)
{
    return m_config->GetNumber("mode", defaultValue);
}

const char* ConfigService::GetLogfile(const char *defaultValue)
{
    return m_config->GetString("logfile", defaultValue);
}

// TODO enum
const char* ConfigService::GetDisplayHat(const char *defaultValue)
{
    return m_config->GetString("displayhat", defaultValue);
}

const char* ConfigService::GetTimezone(const char *defaultValue)
{
    return m_config->GetString("timezone", defaultValue);
}

unsigned ConfigService::GetScreenTimeout(unsigned defaultValue)
{
    return m_config->GetNumber("screen_timeout", defaultValue);
}

unsigned ConfigService::GetST7789Brightness(unsigned defaultValue)
{
    return m_config->GetNumber("st7789_brightness", defaultValue);
}

unsigned ConfigService::GetST7789SleepBrightness(unsigned defaultValue)
{
    return m_config->GetNumber("st7789_sleep_brightness", defaultValue);
}

void ConfigService::SetCurrentImage(const char* value)
{
    m_config->SetString("current_image", value);
}

void ConfigService::SetMode(unsigned value)
{
    m_config->SetNumber("mode", value);
}

void ConfigService::SetDefaultVolume(unsigned value)
{
    m_config->SetNumber("default_volume", value);
}

void ConfigService::SetDisplayHat(const char* value)
{
    m_config->SetString("displayhat", value);
}

void ConfigService::SetTheme(const char* value)
{
    m_config->SetString("theme", value);
}

void ConfigService::SetTimezone(const char* value)
{
    m_config->SetString("timezone", value);
}

void ConfigService::SetLogLevel(unsigned value)
{
    char buffer[2];
    snprintf(buffer, sizeof(buffer), "%u", value);
    m_cmdline->SetValue("loglevel", buffer);
}

void ConfigService::SetLogfile(const char* value)
{
    m_config->SetString("logfile", value);
}

void ConfigService::SetScreenTimeout(unsigned value)
{
    m_config->SetNumber("screen_timeout", value);
}

void ConfigService::SetST7789Brightness(unsigned value)
{
    m_config->SetNumber("st7789_brightness", value);
}

void ConfigService::SetST7789SleepBrightness(unsigned value)
{
    m_config->SetNumber("st7789_sleep_brightness", value);
}

unsigned ConfigService::GetProperty(const char* key, unsigned defaultValue, const char* section)
{
    return m_config->GetNumber(key, defaultValue, section);
}

const char* ConfigService::GetProperty(const char* key, const char* defaultValue, const char* section)
{
    return m_config->GetString(key, defaultValue, section);
}

void ConfigService::SetProperty(const char* key, const char* value, const char* section)
{
    m_config->SetString(key, value, section);
}

void ConfigService::SetProperty(const char* key, unsigned value, const char* section)
{
    m_config->SetNumber(key, value, section);
}

bool ConfigService::IsDirty() {
	return m_config->IsDirty() || m_cmdline->IsDirty();
}

bool ConfigService::Save() {
	bool ok = m_config->Save();

	if (ok) {
		ok = m_cmdline->Save();
	}

	return ok;
}

void ConfigService::Run(void) {
    LOGNOTE("Configservice Run Loop entered");

    // Let the system settle
    CScheduler::Get()->MsSleep(2000);

    while (true) {
            // See if any configuration needs saving. The "Set" method is
            // called from an interrupt and we can't "Save" to disk in an
            // interrupt so we need to do it here
            if (IsDirty()) {
                    LOGNOTE("Saving configuration");
                    Save();
                    LOGNOTE("Saved configuration");
            }

            CScheduler::Get()->MsSleep(100);
    }

}
