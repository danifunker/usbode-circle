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
#include <gitinfo/gitinfo.h>
#include <discimage/util.h>
#include <discimage/cuebinfile.h>
#include "mountpage.h"
#include "util.h"

using namespace kainjow;

LOGMODULE("mountpagehandler");

char s_Mount[] =
#include "mount.h"
;

#define VERSION "2.1.0"

THTTPStatus MountPageHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *m_pCDGadget)
{
	LOGNOTE("Mount page called");

	auto params = parse_query_params(pParams);

	if (params.count("file") == 0)
		return HTTPBadRequest;

	std::string file_name = params["file"];

	LOGNOTE("Got filename %s from parameter", file_name.c_str());

	// Save current mounted image name
	m_pProperties->SelectSection("usbode");
        m_pProperties->SetString("current_image", file_name.c_str());
        m_pProperties->Save();

        // Load the image
        CCueBinFileDevice *cueBinFileDevice = loadCueBinFileDevice(file_name.c_str());
        if (!cueBinFileDevice) {
                LOGERR("Failed to get cueBinFileDevice");
                return HTTPInternalServerError;
        }

	// Set the new device in the CD gadget
        m_pCDGadget->SetDevice(cueBinFileDevice);
        LOGNOTE("CD gadget updated with new image: %s", file_name.c_str());

	// Set up Mustache Template Engine
	mustache::mustache tmpl{s_Mount};
	mustache::data context;

	context.set("image_name", file_name);

	// Find the current USB mode
        // TODO replace with Property get if we move the location of this
        boolean is_full_speed = CKernelOptions::Get()->GetUSBFullSpeed();
        if (is_full_speed)
                context.set("usb_mode", "FullSpeed");
        else
                context.set("usb_mode", "HighSpeed");

        // Add build info
        context.set("version", VERSION);
        context.set("build_info", std::string(GIT_BRANCH) + " @ " + std::string(GIT_COMMIT) + " | " + __DATE__ + " " + __TIME__);

	// Render
	LOGNOTE("Rendering the template");
	std::string rendered = tmpl.render(context);

	if (pBuffer && *pLength >= rendered.length()) {
		memcpy(pBuffer, rendered.c_str(), rendered.length());
		*pLength = rendered.length();
		*ppContentType = "text/html"; // Most likely HTML for a templating engine
		return HTTPOK;
	}
	
	// The provided buffer is too small
	LOGERR("Output buffer too small for Jinjac content.");
	*pLength = 0; // Indicate no content could be written
	*ppContentType = "text/plain"; // Default content type for error
	return HTTPInternalServerError; // Or a more specific error
}
