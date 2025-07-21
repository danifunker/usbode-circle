
#ifndef LISTAPI_HANDLER_H
#define LISTAPI_HANDLER_H

#include "apihandlerbase.h"

class ListAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData);
};
#endif
