#ifndef LOGPAGE_HANDLER_H
#define LOGPAGE_HANDLER_H

#include "pagehandlerbase.h"

class LogPageHandler : public PageHandlerBase {
public:
    THTTPStatus PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties);
    std::string GetHTML();

private:
    // Helper methods
    std::string read_loglines(const std::string& path);
};

#endif
