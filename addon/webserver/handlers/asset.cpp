#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <string>
#include <cstring>
#include <map>
#include <fstream>
#include "asset.h"
#include "../util.h"
#include <configservice/configservice.h>

// Include files for the assets
#include "logo.h"
#include "font-eot.h"
#include "font-woff.h"
#include "favicon.h"
#include "style.h"

LOGMODULE("assethandler");

struct StaticAsset {
    const uint8_t *data;
    size_t length;
    const char *contentType;
};

// route mappings for your assets
static const std::map<std::string, StaticAsset> g_staticAssets = {
    { "/logo.jpg",      { assets_logo_jpg, assets_logo_jpg_len, "image/jpeg" } },
    { "/favicon.ico",   { assets_favicon_ico, assets_favicon_ico_len, "image/x-icon" } },
    { "/style.css",     { (const uint8_t *)assets_style_css, assets_style_css_len, "text/css" } },
    { "/font-eot.eot",  { assets_font_eot_eot, assets_font_eot_eot_len, "application/vnd.ms-fontobject" } },
    { "/font-woff.woff",{ assets_font_woff_woff, assets_font_woff_woff_len, "application/font-woff" } },
    // Add more assets here
};

static const char* GetMimeType(const std::string& path) {
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos)  return "application/javascript";
    if (path.find(".jpg") != std::string::npos) return "image/jpeg";
    if (path.find(".png") != std::string::npos) return "image/png";
    if (path.find(".ico") != std::string::npos) return "image/x-icon";
    return "text/plain";
}

THTTPStatus AssetHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType)
{

    // Sanity checking
    if (!pPath || !pBuffer || !pLength || !ppContentType)
        return HTTPBadRequest;

    static std::string s_themeBasePath;
    static bool s_isInitialized = false;

    // Get active theme, if set
    if (!s_isInitialized) {
        ConfigService* pConfig = ConfigService::Get();
        const char* themeName = pConfig ? pConfig->GetTheme("default") : "default";
        if (strcmp(themeName, "default") != 0) {
            s_themeBasePath = "0:/themes/";
            s_themeBasePath += themeName;
            LOGNOTE("AssetHandler: Theme active: %s", s_themeBasePath.c_str());
        } 
        s_isInitialized = true;
    }

    // Check theme
    if (!s_themeBasePath.empty()) {
        std::string sdPath = s_themeBasePath + pPath;
        std::ifstream file(sdPath.c_str(), std::ios::binary | std::ios::ate);
        if (file.good()) {
            std::streamsize fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            if (fileSize > 0 && (unsigned)fileSize <= *pLength) {
                if (file.read((char*)pBuffer, fileSize)) {
                    *pLength = (unsigned)fileSize;
                    *ppContentType = GetMimeType(sdPath);
                    return HTTPOK;
                }
            }
        }
    }    

    // Find the asset path, returning 404 if not found
    auto it = g_staticAssets.find(pPath);
    if (it == g_staticAssets.end())
        return HTTPNotFound;

    // Get the asset
    const StaticAsset &asset = it->second;
    if (*pLength < asset.length)
        return HTTPInternalServerError;

    // Serve the asset content
    std::memcpy(pBuffer, asset.data, asset.length);
    *pLength = asset.length;
    *ppContentType = asset.contentType;
    return HTTPOK;
}
