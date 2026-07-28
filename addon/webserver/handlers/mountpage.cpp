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

	// Wait for the load to actually happen before saying anything about it.
	// Queueing the request and reporting success announced "Successfully
	// mounted" for images that then refused to load, so the very next page
	// contradicted this one.
	switch (svc->MountByNameAndWait(file_param.c_str())) {
	case SCSITBService::MountOutcome::Success:
		context.set("mounted", true);
		return HTTPOK;

	case SCSITBService::MountOutcome::Failed:
		context.set("mounted", false);
		context.set("mount_failed", true);
		context.set("mount_message", std::string(svc->GetLastMountError()));
		// Stay put rather than bouncing to the homepage; the reason is here.
		context.set("meta_refresh_url", "");
		return HTTPOK;

	case SCSITBService::MountOutcome::Timeout:
		context.set("mounted", false);
		context.set("mount_pending", true);
		context.set("mount_message",
			    std::string("Still loading. Large images on a slow card can take a "
					"while; the homepage will show it once it is ready."));
		return HTTPOK;

	case SCSITBService::MountOutcome::NotFound:
	default:
		break;
	}

	return HTTPNotFound;
}
