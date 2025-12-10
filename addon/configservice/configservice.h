#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <circle/sched/task.h>

// forward declarations
class Config;
class CmdLine;

enum class USBTargetOS : unsigned {
    DosWin = 0,
    Apple = 1,
    Unknown = 255
};

class ConfigService : public CTask 
{
public:
    ConfigService();
    ~ConfigService();

    static ConfigService* Get() { return s_pThis; }

    const char* GetCurrentImage(const char *defaultValue="image.iso");
    unsigned GetDefaultVolume(unsigned defaultValue=255);
    const char* GetDisplayHat(const char *defaultValue="none");
    const char* GetTimezone(const char *defaultValue="UTC");
    unsigned GetScreenTimeout(unsigned defaultValue=30);
    unsigned GetLogLevel(unsigned defaultValue=4);
    unsigned GetMode(unsigned defaultValue=0);
    const char* GetLogfile(const char *defaultValue="0:/usbode-log.txt");
    bool GetUSBFullSpeed();
    unsigned GetST7789Brightness(unsigned defaultValue=1024);
    unsigned GetST7789SleepBrightness(unsigned defaultValue=32);
    u16 GetUSBCDRomVendorId(u16);
    u16 GetUSBCDRomProductId(u16);
    void SetSoundDev(const char* value);
    const char* GetSoundDev(const char* defaultValue="none");
    const char* GetTheme(const char *defaultValue="default");

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
    void SetTheme(const char* value);

    void SetUSBCDRomVendorId(u16 value);
    void SetUSBCDRomProductId(u16 value);
    void SetUSBTargetOS(USBTargetOS value);
    USBTargetOS GetUSBTargetOS(USBTargetOS value=USBTargetOS::DosWin);
    static const char* USBTargetOSToString(USBTargetOS os);
    static USBTargetOS StringToUSBTargetOS(const char* str);

    const char* GetProperty(const char* property, const char* defaultValue, const char* section="usbode");
    unsigned GetProperty(const char* property, unsigned defaultValue, const char* section="usbode");
    void SetProperty(const char* property, unsigned value, const char* section="usbode");
    void SetProperty(const char* property, const char* value, const char* section="usbode");

    bool IsDirty();

    void Run(void);

private:
    CmdLine* m_cmdline;
    Config* m_config;
    static ConfigService *s_pThis;
    bool Save();
};

#endif // CONFIG_SERVICE_H
