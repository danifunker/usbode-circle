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
    LOGNOTE("ListAPIHandler::GetJson called");

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("ListAPIHandler: Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
    }

    // Parse optional path parameter
    std::string path = "";
    auto params = parse_query_params(pParams);
    auto it = params.find("path");
    if (it != params.end()) {
        path = it->second;
        LOGNOTE("ListAPIHandler: path parameter = '%s'", path.c_str());
    }

    // Refresh cache for the specified path (or root if empty)
    LOGNOTE("ListAPIHandler: Calling RefreshCacheForPath");
    svc->RefreshCacheForPath(path.c_str());
    LOGNOTE("ListAPIHandler: RefreshCacheForPath returned, count = %zu", svc->GetCount());

    // Build response
    j["path"] = path;
    j["isRoot"] = path.empty();

    const char* currentPath = svc->GetCurrentCDPath();
    j["currentImage"] = currentPath ? currentPath : "";

    // Build entries array with type information
    LOGNOTE("ListAPIHandler: Building entries array");
    nlohmann::json entries = nlohmann::json::array();
    for (const FileEntry* entry = svc->begin(); entry != svc->end(); ++entry) {
        if (entry == nullptr) continue;
        nlohmann::json item;
        item["name"] = entry->name;
        item["type"] = entry->isDirectory ? "directory" : "file";
        item["size"] = entry->size;
        entries.push_back(item);
    }
    j["entries"] = entries;

    LOGNOTE("ListAPIHandler: GetJson completed successfully");
    return HTTPOK;
}
