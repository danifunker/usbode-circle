#ifndef PAGE_HANDLER_BASE_H
#define PAGE_HANDLER_BASE_H
#include "pagehandler.h"
#include <circle/sched/scheduler.h>
#include <mustache/mustache.hpp>

class PageHandlerBase : public IPageHandler {
public:
    THTTPStatus GetContent(const char *pPath,
                           const char *pParams,
                           const char *pFormData,
                           u8 *pBuffer,
                           unsigned *pLength,
                           const char **ppContentType,
                           CPropertiesFatFsFile *m_pProperties) override;

protected:
    virtual THTTPStatus PopulateContext(kainjow::mustache::data& context,
                            const char *pPath,
                            const char *pParams,
                            const char *pFormData,
                            CPropertiesFatFsFile *m_pProperties) = 0;

    virtual std::string GetHTML() = 0;
};
#endif
