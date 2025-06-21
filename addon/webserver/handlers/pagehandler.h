#ifndef IPAGE_HANDLER_H
#define IPAGE_HANDLER_H

#include <circle/sched/scheduler.h>
#include <Properties/propertiesfatfsfile.h>
#include <usbcdgadget/usbcdgadget.h>

class IPageHandler {
public:
    virtual ~IPageHandler() = default; // Virtual destructor for proper cleanup

    virtual THTTPStatus GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *pCDGadget) = 0;
};

#endif // IPAGE_HANDLER_H
