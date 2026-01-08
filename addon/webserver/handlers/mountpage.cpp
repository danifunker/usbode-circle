#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <scsitbservice/scsitbservice.h>
#include <circle/koptions.h>
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
#include "../util.h"

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
                                   const char  *pFormData)
{
	LOGDBG("Mount page called");

	auto params = parse_query_params(pParams);

	if (params.count("file") == 0)
		return HTTPBadRequest;

	// file parameter is a relative path like "Games/RPG/game.iso" or just "game.iso"
	std::string file_param = params["file"];
	context.set("image_name", file_param);
	context.set("meta_refresh_url", "/");

	LOGDBG("MountPage: Mounting image with relative path: %s", file_param.c_str());

	SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

	if (!svc) {
	    LOGERR("Couldn't fetch SCSITB Service");
        return HTTPInternalServerError;
	}

	// Use SetNextCDByName which now searches by relativePath in the cache
	if (svc->SetNextCDByName(file_param.c_str())) {
		return HTTPOK;
	}

	return HTTPNotFound;
}
