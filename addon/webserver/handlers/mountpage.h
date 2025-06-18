
#ifndef MOUNTPAGE_HANDLER_H
#define MOUNTPAGE_HANDLER_H

#include "pagehandlerbase.h"

class MountPageHandler : public PageHandlerBase {
public:
    THTTPStatus PopulateContext(kainjow::mustache::data& context,
		    		   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *pCDGadget);
    std::string GetHTML();
};
#endif
