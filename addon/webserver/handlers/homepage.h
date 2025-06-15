
#ifndef HOMEPAGE_HANDLER_H
#define HOMEPAGE_HANDLER_H

#include "pagehandler.h"

class HomePageHandler : public IPageHandler {
public:
    THTTPStatus GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *pCDGadget);
};
#endif
