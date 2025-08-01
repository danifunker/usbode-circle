
#ifndef IMAGE_HANDLER_H
#define IMAGE_HANDLER_H

#include "pagehandler.h"

class AssetHandler : public IPageHandler {
public:
    THTTPStatus GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType);
};
#endif
