//
// Host-build stub for <configservice/configservice.h>.
// USBTargetOS matches the real enum. The fake service returns
// test-presettable values; if no "configservice" task is registered with
// the scheduler stub, the gadget falls back to its built-in defaults
// (debug logging off, target OS DosWin), same as on the device.
//
#ifndef _configservice_configservice_h
#define _configservice_configservice_h

#include <circle/sched/task.h>
#include <circle/types.h>

enum class USBTargetOS : unsigned
{
    DosWin = 0,
    Apple = 1,
    Unknown = 255
};

class ConfigService : public CTask
{
public:
    ConfigService(void) {}

    static ConfigService *Get(void) { return s_pThis; }

    unsigned GetProperty(const char *pName, unsigned defaultValue)
    {
        if (debugCdrom && pName != nullptr)
        {
            return 1U;
        }
        return defaultValue;
    }

    USBTargetOS GetUSBTargetOS(USBTargetOS defaultValue = USBTargetOS::DosWin)
    {
        return targetOS;
    }

    // Test-presettable values
    USBTargetOS targetOS = USBTargetOS::DosWin;
    bool debugCdrom = false;

    static ConfigService *s_pThis;
};

#endif
