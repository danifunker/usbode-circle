#include "config.h"
#include <assert.h>
#include <circle/logger.h>
//#include <fatfs/ff.h>
//#include <vector>

LOGMODULE("configimpl");

Config::Config() {
    
    LOGNOTE("Config Constructor");
    m_properties.SetSpaces(false); // no spaces around "="
}

Config::~Config() {
}

bool Config::Load(const char* filename) {
    SI_Error rc = m_properties.LoadFile(filename);
    return (rc == SI_OK);
}

void Config::SetString(const char* key, const char* value, const char* section)
{
    m_properties.SetValue(section, key, value);
    dirty = true;
}

void Config::SetNumber(const char* key, unsigned value, const char* section)
{
    m_properties.SetLongValue(section, key, value);
    dirty = true;
}

unsigned Config::GetNumber(const char* key, unsigned defaultValue, const char* section)
{
    long pv = m_properties.GetLongValue(section, key, defaultValue);
    return static_cast<unsigned>(pv);
}

const char* Config::GetString(const char* key, const char* defaultValue, const char* section)
{
    return m_properties.GetValue(section, key, defaultValue);
}

bool Config::IsDirty() {
 return dirty;
}

bool Config::Save() {
    if (!dirty)
	    return true;

    dirty = false; // don't try again even if this fails, otherwise we risk spinning
    SI_Error rc = m_properties.SaveFile(CONFIG_FILE);
    return (rc == SI_OK);
}
