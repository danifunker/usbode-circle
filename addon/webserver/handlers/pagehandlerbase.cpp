#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <algorithm>
#include <gitinfo/gitinfo.h>
#include "util.h"
#include "pagehandlerbase.h"

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
		   const char **ppContentType,
		   CPropertiesFatFsFile *m_pProperties,
		   CUSBCDGadget *pCDGadget)
{
	// Set up Mustache Template Engine
        mustache::mustache tmpl{s_Template};
	if (!tmpl.is_valid())
		return HTTPInternalServerError;

	// Set up context
        mustache::data context;

	// Fetch the page content from the subclass
	mustache::partial part{[this]() {
	    return GetHTML();
	}};
	context.set("content", mustache::data{part});
	
	// Call subclass hook to add page specific context
	THTTPStatus status = PopulateContext(context, pPath, pParams, pFormData, m_pProperties, pCDGadget);

	// Return HTTP error if necessary
	if (status != HTTPOK)
		return status;
	
	// Get current loaded image
        m_pProperties->Load();
        m_pProperties->SelectSection("usbode");
        std::string current_image = m_pProperties->GetString("current_image", DEFAULT_IMAGE_FILENAME);
	context.set("image_name", current_image);

        // Get the current "mode"
        context.set("cdrom", !m_pProperties->GetNumber("mode", 0));

        // Get the current USB mode
        boolean is_full_speed = CKernelOptions::Get()->GetUSBFullSpeed();
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

