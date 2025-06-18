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

std::string MountPageHandler::GetHTML() {
	return std::string(s_Mount);
}

THTTPStatus MountPageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties,
                                   CUSBCDGadget *pCDGadget)
{
	LOGDBG("Mount page called");

	auto params = parse_query_params(pParams);

	if (params.count("file") == 0)
		return HTTPBadRequest;

	std::string file_name = params["file"];

	LOGDBG("Got filename %s from parameter", file_name.c_str());

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
        pCDGadget->SetDevice(cueBinFileDevice);
        LOGDBG("CD gadget updated with new image: %s", file_name.c_str());

	return HTTPOK;
}
