#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <shutdown/shutdown.h>
#include <string>
#include <cstring>
#include <map>
#include "shutdownapi.h"
#include "util.h"

LOGMODULE("shutdownapi");

THTTPStatus ShutdownAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    if (!pPath)
        return HTTPNotFound;

    std::string path(pPath);
    auto params = parse_query_params(pParams);

    int delay = 500;
    if (params.count("delay")) {
        try {
            delay = std::stoi(params["delay"]);
        } catch (...) {
            // Ignore invalid input, keep default delay
        }
    }

    if (path == "/api/shutdown") {
        new CShutdown(ShutdownHalt, delay);
        j["status"] = "Shutting down in " + std::to_string(delay) + "ms";
        return HTTPOK;
    }

    if (path == "/api/reboot") {
        new CShutdown(ShutdownReboot, delay);
        j["status"] = "Rebooting in " + std::to_string(delay) + "ms";
        return HTTPOK;
    }

    return HTTPNotFound;
}
