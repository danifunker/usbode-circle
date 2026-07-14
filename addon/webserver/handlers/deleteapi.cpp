#include <circle/logger.h>
#include <circle/net/httpdaemon.h>
#include <circle/sched/scheduler.h>
#include <fatfs/ff.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "deleteapi.h"
#include "../util.h"

LOGMODULE("deleteapi");

THTTPStatus DeleteImageAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    auto params = parse_query_params(pParams);
    auto it = params.find("file");
    if (it == params.end() || it->second.empty()) {
        j["status"] = "error";
        j["error"] = "missing file parameter";
        return HTTPOK;
    }

    std::string rel = it->second;
    if (rel.find("..") != std::string::npos || rel[0] == '/') {
        j["status"] = "error";
        j["error"] = "invalid path";
        return HTTPOK;
    }

    SCSITBService* svc = static_cast<SCSITBService*>(
        CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        return HTTPInternalServerError;
    }

    std::string full = "1:/" + rel;

    const char* current = svc->GetCurrentCDPath();
    if (current && full == current) {
        j["status"] = "error";
        j["error"] = "cannot delete the mounted image - mount another one first";
        return HTTPOK;
    }

    FRESULT res = f_unlink(full.c_str());
    if (res != FR_OK) {
        LOGERR("f_unlink(%s) failed: %d", full.c_str(), (int)res);
        j["status"] = "error";
        j["error"] = std::string("delete failed (") +
                     (res == FR_NO_FILE || res == FR_NO_PATH ? "not found" : "fs error") + ")";
        return HTTPOK;
    }

    LOGNOTE("Deleted image %s", full.c_str());
    svc->RefreshCache();

    j["status"] = "ok";
    j["deleted"] = rel;
    return HTTPOK;
}
