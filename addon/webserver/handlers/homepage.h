
#ifndef HOMEPAGE_HANDLER_H
#define HOMEPAGE_HANDLER_H

#include "pagehandlerbase.h"

class HomePageHandler : public PageHandlerBase {
public:
    THTTPStatus PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData);
    std::string GetHTML();
};
#endif
