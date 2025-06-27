#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <string>
#include <cstring>
#include <map>
#include "asset.h"
#include "util.h"

// Include files for the assets
#include "logo.h"
#include "favicon.h"

LOGMODULE("assethandler");

struct StaticAsset {
    const uint8_t *data;
    size_t length;
    const char *contentType;
};

// route mappings for your asset
static const std::map<std::string, StaticAsset> g_staticAssets = {
    { "/logo.jpg",     { assets_logo_jpg, assets_logo_jpg_len, "image/jpeg" } },
    { "/favicon.ico",  { assets_favicon_ico, assets_favicon_ico_len, "image/x-icon" } },
    // Add more assets here
};

THTTPStatus AssetHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties)
{

    // Sanity checking
    if (!pPath || !pBuffer || !pLength || !ppContentType)
        return HTTPBadRequest;

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
