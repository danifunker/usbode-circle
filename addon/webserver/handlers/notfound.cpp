#include <circle/logger.h>
#include <circle/net/httpdaemon.h>
#include "notfound.h"

LOGMODULE("notfoundpagehandler");

THTTPStatus NotFoundPageHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *pCDGadget)
{
	LOGNOTE("Not Found page handler");
	return HTTPNotFound;
}
