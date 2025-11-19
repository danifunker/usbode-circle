// discimage/plugindevice.h
#ifndef _PLUGINDEVICE_H
#define _PLUGINDEVICE_H

#include "cuedevice.h"
#include "imageplugin.h"

// This wraps an IImagePlugin and presents it as an ICueDevice
class CPluginDevice : public ICueDevice {
public:
    CPluginDevice(IImagePlugin* plugin);
    ~CPluginDevice() override;
    
    // ICueDevice interface
    int Read(void* pBuffer, size_t nCount) override;
    int Write(const void* pBuffer, size_t nCount) override;
    u64 Seek(u64 ullOffset) override;
    u64 GetSize(void) const override;
    u64 Tell() const override;
    const char* GetCueSheet() const override;
    MEDIA_TYPE GetMediaType() const override;
    
    // Extended interface for subchannel access
    IImagePlugin* GetPlugin() { return m_plugin; }
    
private:
    IImagePlugin* m_plugin;
    u64 m_currentPosition = 0;
};

#endif