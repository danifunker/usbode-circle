#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

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
    const char* GetTimezone(const char *defaultValue="UTC");
    unsigned GetScreenTimeout(unsigned defaultValue=30);
    unsigned GetLogLevel(unsigned defaultValue=4);
    unsigned GetMode(unsigned defaultValue=0);
    const char* GetLogfile(const char *defaultValue="SD:/usbode-log.txt");
    bool GetUSBFullSpeed();
    unsigned GetST7789Brightness(unsigned defaultValue=1024);
    unsigned GetST7789SleepBrightness(unsigned defaultValue=32);

    void SetSoundDev(const char* value);
    const char* GetSoundDev(const char* defaultValue="none");

    void SetLogfile(const char* value);
    void SetCurrentImage(const char* value);
    void SetDefaultVolume(unsigned value);
    void SetDisplayHat(const char* value);
    void SetTimezone(const char* value);
    void SetScreenTimeout(unsigned value);
    void SetLogLevel(unsigned value);
    void SetMode(unsigned value);
    void SetUSBFullSpeed(bool value);
    void SetST7789Brightness(unsigned value);
    void SetST7789SleepBrightness(unsigned value);

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
