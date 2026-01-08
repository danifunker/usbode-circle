#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "listapi.h"
#include "../util.h"

LOGMODULE("listapi");

THTTPStatus ListAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
    }

    // Parse optional path parameter
    std::string path = "";
    auto params = parse_query_params(pParams);
    auto it = params.find("path");
    if (it != params.end()) {
        path = it->second;
    }

    // Refresh cache for the specified path (or root if empty)
    svc->RefreshCacheForPath(path.c_str());

    // Build response
    j["path"] = path;
    j["isRoot"] = path.empty();
    j["currentImage"] = svc->GetCurrentCDPath();

    // Build entries array with type information
    nlohmann::json entries = nlohmann::json::array();
    for (const FileEntry* entry = svc->begin(); entry != svc->end(); ++entry) {
        nlohmann::json item;
        item["name"] = entry->name;
        item["type"] = entry->isDirectory ? "directory" : "file";
        item["size"] = entry->size;
        entries.push_back(item);
    }
    j["entries"] = entries;

    return HTTPOK;
}
