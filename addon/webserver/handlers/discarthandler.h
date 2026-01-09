#ifndef DISCART_HANDLER_H
#define DISCART_HANDLER_H

#include "pagehandler.h"

class DiscArtHandler : public IPageHandler {
public:
    THTTPStatus GetContent(const char* pPath,
                           const char* pParams,
                           const char* pFormData,
                           u8* pBuffer,
                           unsigned* pLength,
                           const char** ppContentType);
};

#endif // DISCART_HANDLER_H
