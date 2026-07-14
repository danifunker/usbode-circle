#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <string>
#include "tracepage.h"

using namespace kainjow;

LOGMODULE("tracepagehandler");

char s_Trace[] =
#include "trace.h"
;

std::string TracePageHandler::GetHTML() {
    return std::string(s_Trace);
}

THTTPStatus TracePageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData)
{
    // All live state comes from /api/trace via the page's script; the page
    // itself is static.
    return HTTPOK;
}
