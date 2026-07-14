#ifndef _webserver_handlers_deleteapi_h
#define _webserver_handlers_deleteapi_h

#include "apihandlerbase.h"

// DELETE an image from the images volume: /api/images/delete?file=<relativePath>
// Refuses to delete the currently mounted image.
class DeleteImageAPIHandler : public APIHandlerBase
{
public:
    THTTPStatus GetJson(nlohmann::json& j,
                        const char *pPath,
                        const char *pParams,
                        const char *pFormData) override;
};

#endif
