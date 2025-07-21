#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <shutdown/shutdown.h>
#include "shutdownpage.h"
#include "util.h"

using namespace kainjow;

LOGMODULE("shutdownpagehandler");

char s_Shutdown[] =
#include "shutdown.h"
;

std::string ShutdownPageHandler::GetHTML() {
    return std::string(s_Shutdown);
}

THTTPStatus ShutdownPageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData)
{
    LOGNOTE("Shutdown page called");
    
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

    if (path == "/shutdown") {
        new CShutdown(ShutdownHalt, delay);
        context["status"] = "Shutting down...";
        return HTTPOK;
    }

    if (path == "/reboot") {
        new CShutdown(ShutdownReboot, delay);
        context["status"] = "Rebooting...";
        return HTTPOK;
    }

    return HTTPNotFound;
}
