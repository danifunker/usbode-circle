#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "mountapi.h"
#include "util.h"

LOGMODULE("mountapi");

THTTPStatus MountAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData,
                CPropertiesFatFsFile *m_pProperties,
                CUSBCDGadget *pCDGadget) 
{

    auto params = parse_query_params(pParams);

    if (params.count("file") == 0)
            return HTTPBadRequest;

    std::string file_name = params["file"];

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
            LOGERR("Couldn't fetch SCSITB Service");
            return HTTPInternalServerError;
    }

    if (svc->SetNextCDByName(file_name.c_str())) {
	    
	    j = {
		    {"status", "ok"}
	    };
	    return HTTPOK;
    }

    return HTTPNotFound;

}
