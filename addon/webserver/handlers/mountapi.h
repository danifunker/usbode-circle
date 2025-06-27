
#ifndef MOUNTAPI_HANDLER_H
#define MOUNTAPI_HANDLER_H

#include "apihandlerbase.h"

class MountAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData,
		CPropertiesFatFsFile *m_pProperties,
		CUSBCDGadget *pCDGadget);
};
#endif
