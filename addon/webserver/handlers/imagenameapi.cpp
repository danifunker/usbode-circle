#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "imagenameapi.h"
#include "../util.h"

LOGMODULE("imagenameapi");

THTTPStatus ImageNameAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
            LOGERR("Couldn't fetch SCSITB Service");
            return HTTPInternalServerError;
    }

    j = {
	    {"name", svc->GetCurrentCDName()}
    };

    // Mounting finishes long after the request was answered "ok", so a failure
    // has no other way back to the user. The page already polls this endpoint.
    const char* mountError = svc->GetLastMountError();
    if (mountError != nullptr && mountError[0] != '\0') {
	    j["mount_error"] = mountError;
    }

    return HTTPOK;

}
