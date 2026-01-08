#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "mountapi.h"
#include "../util.h"

LOGMODULE("mountapi");

THTTPStatus MountAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    auto params = parse_query_params(pParams);

    if (params.count("file") == 0)
        return HTTPBadRequest;

    // file parameter can be a relative path like "Games/RPG/game.iso"
    // URL decoding has already converted %2F to /
    std::string file_param = params["file"];

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
    }

    // Construct full path: "1:/" + file_param
    char fullPath[MAX_PATH_LEN];
    snprintf(fullPath, sizeof(fullPath), "1:/%s", file_param.c_str());

    LOGNOTE("MountAPI: Mounting image at path: %s", fullPath);

    if (svc->SetNextCDByPath(fullPath)) {
        j = {{"status", "ok"}};
        return HTTPOK;
    }

    return HTTPNotFound;
}
