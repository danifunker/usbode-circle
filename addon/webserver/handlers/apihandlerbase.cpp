#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <algorithm>
#include <gitinfo/gitinfo.h>
#include "util.h"
#include "apihandlerbase.h"

using json = nlohmann::json;

LOGMODULE("apihandlerbase");

THTTPStatus APIHandlerBase::GetContent(const char *pPath,
                   const char *pParams,
                   const char *pFormData,
                   u8 *pBuffer,
                   unsigned *pLength,
                   const char **ppContentType,
                   CPropertiesFatFsFile *m_pProperties,
                   CUSBCDGadget *pCDGadget)
{
        // Call subclass hook to add page specific context
        json j;
        THTTPStatus status = GetJson(j, pPath, pParams, pFormData, m_pProperties, pCDGadget);

        // Return HTTP error if necessary
        if (status != HTTPOK)
                return status;

        std::string rendered = j.dump();

        *ppContentType = "application/json";
        if (pBuffer && *pLength >= rendered.length()) {
            memcpy(pBuffer, rendered.c_str(), rendered.length());
            *pLength = rendered.length();
            return HTTPOK;
        }

        // The provided buffer is too small
        LOGERR("Output buffer too small for rendered content.");
        *pLength = 0;
        return HTTPInternalServerError;

}
