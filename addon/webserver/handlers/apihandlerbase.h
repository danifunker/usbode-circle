#ifndef API_HANDLER_BASE_H
#define API_HANDLER_BASE_H
#include "pagehandler.h"
#include <circle/sched/scheduler.h>
#include <json/json.hpp>

class APIHandlerBase : public IPageHandler {
public:
    THTTPStatus GetContent(const char *pPath,
                           const char *pParams,
                           const char *pFormData,
                           u8 *pBuffer,
                           unsigned *pLength,
                           const char **ppContentType,
                           CPropertiesFatFsFile *m_pProperties,
                           CUSBCDGadget *pCDGadget) override;

protected:
    virtual THTTPStatus GetJson(nlohmann::json& j,
                            const char *pPath,
                            const char *pParams,
                            const char *pFormData,
                            CPropertiesFatFsFile *m_pProperties,
                            CUSBCDGadget *pCDGadget) = 0;
};
#endif
