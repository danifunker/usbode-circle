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
        // Normalize: remove trailing slash
        while (!path.empty() && path.back() == '/')
            path.pop_back();
    }

    bool isRoot = path.empty();
    size_t pathLen = path.length();

    // Build response
    j["path"] = path;
    j["isRoot"] = isRoot;

    const char* currentPath = svc->GetCurrentCDPath();
    j["currentImage"] = currentPath ? currentPath : "";

    // Build entries array - iterate and filter
    LOGNOTE("ListAPIHandler: Building entries array");
    nlohmann::json entries = nlohmann::json::array();
    
    for (const FileEntry* entry = svc->begin(); entry != svc->end(); ++entry) {
        const char* entryPath = entry->relativePath;
        
        // Filter logic
        bool showEntry = false;
        if (isRoot) {
            showEntry = (strchr(entryPath, '/') == nullptr);
        } else {
            if (strncmp(entryPath, path.c_str(), pathLen) == 0 && entryPath[pathLen] == '/') {
                const char* remainder = entryPath + pathLen + 1;
                showEntry = (strchr(remainder, '/') == nullptr);
            }
        }
        
        if (!showEntry)
            continue;
        
        nlohmann::json item;
        item["name"] = entry->name;
        item["relativePath"] = entry->relativePath;
        item["type"] = entry->isDirectory ? "directory" : "file";
        item["size"] = entry->size;
        entries.push_back(item);
    }
    
    j["entries"] = entries;

    LOGNOTE("ListAPIHandler: GetJson completed successfully");
    return HTTPOK;
}
