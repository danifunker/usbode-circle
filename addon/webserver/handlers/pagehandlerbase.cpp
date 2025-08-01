#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <scsitbservice/scsitbservice.h>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <algorithm>
#include <gitinfo/gitinfo.h>
#include "util.h"
#include "pagehandlerbase.h"
#include <configservice/configservice.h>

using namespace kainjow;

LOGMODULE("pagehandlerbase");

char s_Template[] =
#include "template.h"
;

THTTPStatus PageHandlerBase::GetContent(const char *pPath,
		   const char *pParams,
		   const char *pFormData,
		   u8 *pBuffer,
		   unsigned *pLength,
		   const char **ppContentType)
{
	// Set up Mustache Template Engine
        mustache::mustache tmpl{s_Template};
	if (!tmpl.is_valid())
		return HTTPInternalServerError;

	// Set up context
        mustache::data context;

	// Set up context defaults
	context.set("meta_refresh_timeout", "5");

	// Fetch the page content from the subclass
	mustache::partial part{[this]() {
	    return GetHTML();
	}};
	context.set("content", mustache::data{part});
	
	// Call subclass hook to add page specific context
	THTTPStatus status = PopulateContext(context, pPath, pParams, pFormData);

	// Return HTTP error if necessary
	if (status != HTTPOK)
		return status;
	
	// Get current loaded image
        SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
        if (!svc)
            return HTTPInternalServerError;

        // Get current loaded image
        std::string current_image = svc->GetCurrentCDName();

	// Get our config service
	ConfigService* config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

	// Get the current mode
        context.set("cdrom", !config->GetMode());

        // Get the current USB mode
        boolean is_full_speed = config->GetUSBFullSpeed();
        context.set("usb_mode", is_full_speed?"FullSpeed":"HighSpeed");

        // Add build info
        context.set("version", CGitInfo::Get()->GetVersionWithBuildString());
        context.set("build_info", std::string(GIT_BRANCH) + " @ " + std::string(GIT_COMMIT) + " | " + __DATE__ + " " + __TIME__);

	// Render
        LOGDBG("Rendering the template");
        std::string rendered = tmpl.render(context);

        if (pBuffer && *pLength >= rendered.length()) {
            memcpy(pBuffer, rendered.c_str(), rendered.length());
            *pLength = rendered.length();
            *ppContentType = "text/html";
            return HTTPOK;
        }

        // The provided buffer is too small
        LOGERR("Output buffer too small for rendered content.");
        *pLength = 0;
        *ppContentType = "text/plain";
        return HTTPInternalServerError;

}

