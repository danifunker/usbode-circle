#include <circle/logger.h>
#include <circle/util.h>
#include <map>
#include <string>
#include <cstring>

#include "pagehandlerregistry.h"

// includes for your page handlers
#include "handlers/homepage.h"
#include "handlers/mountpage.h"
#include "handlers/modepage.h"
#include "handlers/configpage.h"
#include "handlers/logpage.h"
#include "handlers/asset.h"

// instances of your page handlers
static HomePageHandler s_homePageHandler;
static MountPageHandler s_mountPageHandler;
static ModePageHandler s_modePageHandler;
static ConfigPageHandler s_configPageHandler;
static LogPageHandler s_logPageHandler;
static AssetHandler s_assetHandler;

// routes for your page handlers
static const std::map<std::string, IPageHandler*> g_pageHandlers = {
    { "/",      &s_homePageHandler },
    { "/mount", &s_mountPageHandler },
    { "/switchmode", &s_modePageHandler },
    { "/config", &s_configPageHandler },
    { "/log", &s_logPageHandler },
    // More routes can be added here
};

IPageHandler* PageHandlerRegistry::getHandler(const char* path) {

    if (!path)
        return &s_assetHandler;

    auto it = g_pageHandlers.find(path);
    if (it != g_pageHandlers.end()) {
        return it->second;
    }

    // No page handler found so assume it's an asset. If it's
    // not, the asset handler will return a 404
    return &s_assetHandler;
}

