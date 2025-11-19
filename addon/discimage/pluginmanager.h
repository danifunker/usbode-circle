// discimage/pluginmanager.h
#ifndef _PLUGINMANAGER_H
#define _PLUGINMANAGER_H

#include "imageplugin.h"

class CPluginManager {
public:
    static CPluginManager* Get();
    
    // Register plugins at startup
    void RegisterPlugin(IImagePlugin* plugin);
    
    // Find appropriate plugin for a file
    IImagePlugin* GetPluginForFile(const char* filename);
    
    // Create a device wrapper around the plugin
    ICueDevice* LoadImage(const char* filename);
    
private:
    CPluginManager();
    static CPluginManager* s_pThis;
    
    IImagePlugin* m_plugins[10];  // Support up to 10 plugins
    int m_pluginCount = 0;
};

#endif