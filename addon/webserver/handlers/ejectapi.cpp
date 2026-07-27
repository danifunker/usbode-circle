#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <circle/sched/scheduler.h>
#include <json/json.hpp>
#include <scsitbservice/scsitbservice.h>
#include <string>
#include <cstring>
#include <map>
#include "ejectapi.h"
#include "../util.h"

LOGMODULE("ejectapi");

THTTPStatus EjectAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    auto params = parse_query_params(pParams);

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
    }

    bool insert = params.count("insert") != 0 && params["insert"] != "0";

    if (insert) {
        LOGNOTE("EjectAPI: inserting medium");
        svc->SetPendingInsert();
        j = {{"status", "ok"}, {"ejected", false}};
    } else {
        LOGNOTE("EjectAPI: ejecting medium");
        svc->SetPendingEject();
        j = {{"status", "ok"}, {"ejected", true}};
    }

    return HTTPOK;
}
