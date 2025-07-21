
#ifndef SHUTDOWNAPI_HANDLER_H
#define SHUTDOWNAPI_HANDLER_H

#include "apihandlerbase.h"

class ShutdownAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData);
};
#endif
