#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "imagenameapi.h"
#include "util.h"

LOGMODULE("imagenameapi");

THTTPStatus ImageNameAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData,
                CPropertiesFatFsFile *m_pProperties)
{

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
            LOGERR("Couldn't fetch SCSITB Service");
            return HTTPInternalServerError;
    }

    j = {
	    {"name", svc->GetCurrentCDName()}
    };
    return HTTPOK;

}
