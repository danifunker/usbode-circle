#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <circle/sched/task.h>
#include <circle/sysconfig.h> 
// forward declarations
class Config;
class CmdLine;

#ifndef USB_GADGET_DEVICE_ID_CD
#define USB_GADGET_DEVICE_ID_CD 0x1d6b
#endif

class ConfigService : public CTask
{
public:
    ConfigService();
    ~ConfigService();
    const char *GetCurrentImage(const char *defaultValue = "image.iso");
    unsigned GetDefaultVolume(unsigned defaultValue = 255);
    const char *GetDisplayHat(const char *defaultValue = "none");
    const char *GetTimezone(const char *defaultValue = "UTC");
    unsigned GetScreenTimeout(unsigned defaultValue = 30);
    unsigned GetLogLevel(unsigned defaultValue = 4);
    unsigned GetMode(unsigned defaultValue = 0);
    const char *GetLogfile(const char *defaultValue = "0:/usbode-log.txt");
    bool GetUSBFullSpeed();
    unsigned GetST7789Brightness(unsigned defaultValue = 1024);
    unsigned GetST7789SleepBrightness(unsigned defaultValue = 32);
    u16 GetUSBCDRomVendorId(u16 defaultValue = USB_GADGET_VENDOR_ID);
    u16 GetUSBCDRomProductId(u16 defaultValue = USB_GADGET_DEVICE_ID_CD);
    void SetSoundDev(const char *value);
    const char *GetSoundDev(const char *defaultValue = "none");

    void SetLogfile(const char *value);
    void SetCurrentImage(const char *value);
    void SetDefaultVolume(unsigned value);
    void SetDisplayHat(const char *value);
    void SetTimezone(const char *value);
    void SetScreenTimeout(unsigned value);
    void SetLogLevel(unsigned value);
    void SetMode(unsigned value);
    void SetUSBFullSpeed(bool value);
    void SetST7789Brightness(unsigned value);
    void SetST7789SleepBrightness(unsigned value);
    const char *GetUSBProtocol(const char *defaultValue = "standard");
    void SetUSBProtocol(const char *value);
    void SetUSBCDRomVendorId(u16 value);
    void SetUSBCDRomProductId(u16 value);

    const char *GetProperty(const char *property, const char *defaultValue, const char *section = "usbode");
    unsigned GetProperty(const char *property, unsigned defaultValue, const char *section = "usbode");
    void SetProperty(const char *property, unsigned value, const char *section = "usbode");
    void SetProperty(const char *property, const char *value, const char *section = "usbode");

    bool IsDirty();

    void Run(void);

private:
    CmdLine *m_cmdline;
    Config *m_config;
    static ConfigService *s_pThis;
    bool Save();
};

#endif // CONFIG_SERVICE_H
