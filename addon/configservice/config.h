#ifndef CONFIG_IMPL_H
#define CONFIG_IMPL_H

#include "simpleini.hpp"

#define CONFIG_FILE "0:/config.txt"

class Config
{
public:

    Config();
    ~Config();

    void SetString(const char* key, const char* value, const char* section="usbode");
    void SetNumber(const char* key, unsigned value, const char* section="usbode");
    unsigned GetNumber(const char* key, unsigned defaultValue, const char* section="usbode");
    const char* GetString(const char* key, const char* defaultValue, const char* section="usbode");
    bool Load(const char* filename);
    bool Save();
    bool IsDirty();
private:
    CSimpleIniA m_properties;
    bool dirty = false;

};

#endif
