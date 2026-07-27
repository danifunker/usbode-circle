
#ifndef EJECTAPI_HANDLER_H
#define EJECTAPI_HANDLER_H

#include "apihandlerbase.h"

// /api/eject         -> eject the current medium (report empty drive)
// /api/eject?insert=1 -> re-insert the previously ejected medium
class EjectAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData);
};
#endif
