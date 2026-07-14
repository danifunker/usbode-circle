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
#include "handlers/shutdownpage.h"
#include "handlers/asset.h"

// includes for your api handlers
#include "handlers/mountapi.h"
#include "handlers/listapi.h"
#include "handlers/shutdownapi.h"
#include "handlers/imagenameapi.h"
#include "handlers/traceapi.h"
#include "handlers/tracepage.h"
#include "handlers/discarthandler.h"
#include "handlers/deleteapi.h"

// instances of your page handlers
static HomePageHandler s_homePageHandler;
static MountPageHandler s_mountPageHandler;
static ModePageHandler s_modePageHandler;
static ConfigPageHandler s_configPageHandler;
static LogPageHandler s_logPageHandler;
static AssetHandler s_assetHandler;
static ShutdownPageHandler s_shutdownPageHandler;

// instances of your API handlers
static MountAPIHandler s_mountAPIHandler;
static ListAPIHandler s_listAPIHandler;
static ShutdownAPIHandler s_shutdownAPIHandler;
static ImageNameAPIHandler s_imageNameAPIHandler;
static TraceAPIHandler s_traceAPIHandler;
static TraceDownloadHandler s_traceDownloadHandler;
static TracePageHandler s_tracePageHandler;
static DiscArtHandler s_discArtHandler;
static DeleteImageAPIHandler s_deleteImageAPIHandler;

// routes for your handlers
static const std::map<std::string, IPageHandler*> g_pageHandlers = {
    // Pages
    { "/",      &s_homePageHandler },
    { "/mount", &s_mountPageHandler },
    { "/switchmode", &s_modePageHandler },
    { "/config", &s_configPageHandler },
    { "/log", &s_logPageHandler },
    { "/shutdown", &s_shutdownPageHandler },
    { "/reboot", &s_shutdownPageHandler },

    // API
    { "/api/mount", &s_mountAPIHandler },
    { "/api/list", &s_listAPIHandler },
    { "/api/shutdown", &s_shutdownAPIHandler },
    { "/api/reboot", &s_shutdownAPIHandler },
    { "/api/imagename", &s_imageNameAPIHandler },
    { "/api/trace", &s_traceAPIHandler },
    { "/api/trace/start", &s_traceAPIHandler },
    { "/api/trace/stop", &s_traceAPIHandler },
    { "/api/trace/save", &s_traceAPIHandler },
    { "/usbode.utrace", &s_traceDownloadHandler },
    { "/trace", &s_tracePageHandler },
    // /api/images/upload is dispatched directly in CWebServer::GetContent
    // (it needs the protected multipart API), not through this registry.
    { "/api/images/delete", &s_deleteImageAPIHandler },

    // Disc art
    { "/discart", &s_discArtHandler },
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

