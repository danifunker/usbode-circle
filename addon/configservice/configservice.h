#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#define CONFIG_FILE "config.txt"

#include <Properties/propertiesfatfsfile.h>

class ConfigService
{
public:
    static ConfigService* GetInstance();
    const char* GetCurrentImage(const char *defaultValue="image.iso");
    unsigned GetDefaultVolume(unsigned defaultValue=255);
    const char* GetDisplayHat(const char *defaultValue="none");
    unsigned GetScreenTimeout(unsigned defaultValue=30);
    const char* GetLogfile(const char *defaultValue="SD:/usbode-logs.txt");

    void SetLogfile(const char* value);
    void SetCurrentImage(const char* value);
    void SetDefaultVolume(unsigned value);
    void SetDisplayHat(const char* value);
    void SetScreenTimeout(unsigned value);

private:
    ConfigService();
    ~ConfigService();
    ConfigService(const ConfigService&);
    ConfigService& operator=(const ConfigService&);
    CPropertiesFatFsFile* m_properties;
};

#endif // CONFIG_SERVICE_H
