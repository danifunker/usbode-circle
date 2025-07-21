#ifndef SHUTDOWNPAGE_HANDLER_H
#define SHUTDOWNPAGE_HANDLER_H

#include "pagehandlerbase.h"

class ShutdownPageHandler : public PageHandlerBase {
public:
    THTTPStatus PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData);
    std::string GetHTML();

private:
};

#endif
