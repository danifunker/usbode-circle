#include "configservice.h"
#include <circle/logger.h>
#include <string.h>
#include <ctype.h>
#include <fatfs/ff.h>
#include "../../src/kernel.h"

//TODO: Find a way to delete settings
// deal with cmdline.txt. Can we just put in config.txt?

static ConfigService* s_instance = nullptr;

ConfigService* ConfigService::GetInstance()
{
    if (s_instance == nullptr) {
        s_instance = new ConfigService();
    }
    return s_instance;
}

ConfigService::ConfigService()
{
    FATFS* fs = CKernel::Get()->GetFileSystem();
    m_properties = new CPropertiesFatFsFile(CONFIG_FILE, fs);
}

ConfigService::~ConfigService()
{
    delete m_properties;
    s_instance = nullptr;
}

const char* ConfigService::GetCurrentImage(const char *defaultValue)
{
	return m_properties->GetString("current_image", defaultValue);
}

unsigned ConfigService::GetDefaultVolume(unsigned defaultValue)
{
	return m_properties->GetNumber("default_volume", defaultValue);
}

const char* ConfigService::GetLogfile(const char *defaultValue)
{
	return m_properties->GetString("logfile", defaultValue);
}

const char* ConfigService::GetDisplayHat(const char *defaultValue)
{
	return m_properties->GetString("displayhat", defaultValue);
}

unsigned ConfigService::GetScreenTimeout(unsigned defaultValue)
{
	return m_properties->GetNumber("screen_timeout", defaultValue);
}

void ConfigService::SetCurrentImage(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("current_image", value);
    m_properties->Save();
}

void ConfigService::SetDefaultVolume(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("default_volume", value);
    m_properties->Save();
}

void ConfigService::SetDisplayHat(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("displayhat", value);
    m_properties->Save();
}

void ConfigService::SetLogfile(const char* value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetString("logfile", value);
    m_properties->Save();
}

void ConfigService::SetScreenTimeout(unsigned value)
{
    m_properties->SelectSection("usbode");
    m_properties->SetNumber("screen_timeout", value);
    m_properties->Save();
}
