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
#include <shutdown/shutdown.h>
#include "modepage.h"
#include "util.h"

using namespace kainjow;

LOGMODULE("modepagehandler");

char s_Mode[] =
#include "mode.h"
;

std::string ModePageHandler::GetHTML() {
	return std::string(s_Mode);
}

THTTPStatus ModePageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties,
                                   CUSBCDGadget *pCDGadget)
{
	LOGDBG("Mode page called");

	auto params = parse_query_params(pParams);

	if (params.count("mode") == 0)
		return HTTPBadRequest;

	int qmode;
	try {
	    qmode = std::stoi(params["mode"]);
	    if (qmode != 0 && qmode != 1)
		return HTTPBadRequest;
	} catch (const std::invalid_argument& e) {
	    return HTTPBadRequest; // Not a valid int
	} catch (const std::out_of_range& e) {
	    return HTTPBadRequest; // Too large/small
	}

	// Compare to current mode & proceed if necessary
	int mode = m_pProperties->GetNumber("mode", 0);
	LOGDBG("Mode parameter is %d", mode);

	if (mode != qmode) {

		// Save current mode
		m_pProperties->SelectSection("usbode");
		m_pProperties->SetNumber("mode", qmode, 10);
		m_pProperties->Save();

		// Signal a reboot or shutdown
		new CShutdown(ShutdownReboot, 1000);
	}

	return HTTPOK;
}
