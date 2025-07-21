#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#define CONFIG_FILE "config.txt"

#include "cmdline.h"
#include <Properties/propertiesfatfsfile.h>
#include <circle/sched/task.h>

class ConfigService : public CTask 
{
public:
    ConfigService();
    ~ConfigService();
    const char* GetCurrentImage(const char *defaultValue="image.iso");
    unsigned GetDefaultVolume(unsigned defaultValue=255);
    const char* GetDisplayHat(const char *defaultValue="none");
    unsigned GetScreenTimeout(unsigned defaultValue=30);
    const char* GetLogfile(const char *defaultValue="SD:/usbode-logs.txt");
    bool GetUSBFullSpeed();

    void SetLogfile(const char* value);
    void SetCurrentImage(const char* value);
    void SetDefaultVolume(unsigned value);
    void SetDisplayHat(const char* value);
    void SetScreenTimeout(unsigned value);
    void SetUSBFullSpeed(bool value);

    bool IsDirty();

    void Run(void);

private:
    CPropertiesFatFsFile* m_properties;
    CmdLine cmdline;
    bool cmdlineIsDirty = false;
    bool configIsDirty = false;
    static ConfigService *s_pThis;
    bool Save();
};

#endif // CONFIG_SERVICE_H
