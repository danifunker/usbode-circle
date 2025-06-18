#ifndef CONFIGPAGE_HANDLER_H
#define CONFIGPAGE_HANDLER_H

#include "pagehandlerbase.h"

class ConfigPageHandler : public PageHandlerBase {
public:
    THTTPStatus PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties,
                                   CUSBCDGadget *pCDGadget);
    std::string GetHTML();

private:
    // Helper methods
    bool UpdateConfigFile(const std::map<std::string, std::string>& config_params, std::string& error_message);
    bool UpdateCmdlineFile(const std::map<std::string, std::string>& cmdline_params, std::string& error_message);
    std::map<std::string, std::string> ParseConfigFile();
    std::map<std::string, std::string> ParseCmdlineFile();
    std::map<std::string, std::string> ParseFormData(const char* pFormData);
};

#endif
