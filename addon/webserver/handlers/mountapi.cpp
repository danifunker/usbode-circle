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

    // file parameter is a relative path like "Games/RPG/game.iso" or just "game.iso"
    // URL decoding has already converted %2F to /
    std::string file_param = params["file"];

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
    }

    LOGNOTE("MountAPI: Mounting image with relative path: %s", file_param.c_str());

    // Use SetNextCDByName which now searches by relativePath in the cache
    if (svc->SetNextCDByName(file_param.c_str())) {
        j = {{"status", "ok"}};
        return HTTPOK;
    }

    return HTTPNotFound;
}
