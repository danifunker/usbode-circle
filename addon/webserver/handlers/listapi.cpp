#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "listapi.h"
#include "util.h"

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

    for (const FileEntry* it = svc->begin(); it != svc->end(); ++it) {
	    j["names"].push_back(it->name);
    }

    return HTTPOK;

}
