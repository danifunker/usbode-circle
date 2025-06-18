#include <circle/logger.h>
#include <circle/util.h>
#include <map>
#include <string>
#include <cstring>

#include "pagehandlerregistry.h"
#include "handlers/homepage.h"
#include "handlers/mountpage.h"
#include "handlers/asset.h"

// Static handler instances
static HomePageHandler s_homePageHandler;
static MountPageHandler s_mountPageHandler;
static AssetHandler s_assetHandler;

// Handler registry
static const std::map<std::string, IPageHandler*> g_pageHandlers = {
    { "/",      &s_homePageHandler },
    { "/mount", &s_mountPageHandler },
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

