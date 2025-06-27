
#ifndef IMAGENAMEAPI_HANDLER_H
#define IMAGENAMEAPI_HANDLER_H

#include "apihandlerbase.h"

class ImageNameAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData,
		CPropertiesFatFsFile *m_pProperties,
		CUSBCDGadget *pCDGadget);
};
#endif
