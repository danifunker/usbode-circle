
#ifndef TRACEAPI_HANDLER_H
#define TRACEAPI_HANDLER_H

#include "apihandlerbase.h"

class TraceAPIHandler : public APIHandlerBase {
public:
   THTTPStatus GetJson(nlohmann::json& j,
		const char *pPath,
		const char *pParams,
		const char *pFormData);
};

// Serves the capture as a binary .utrace download (route /usbode.utrace,
// so the browser saves it under that name).
class TraceDownloadHandler : public IPageHandler {
public:
    THTTPStatus GetContent(const char* pPath,
                           const char* pParams,
                           const char* pFormData,
                           u8* pBuffer,
                           unsigned* pLength,
                           const char** ppContentType);
};
#endif
