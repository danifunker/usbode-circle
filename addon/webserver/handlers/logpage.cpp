#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <shutdown/shutdown.h>
#include "logpage.h"
#include "util.h"

using namespace kainjow;

LOGMODULE("logpagehandler");

char s_Log[] =
#include "log.h"
;

std::string LogPageHandler::GetHTML() {
    return std::string(s_Log);
}

std::string LogPageHandler::read_loglines(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }

    std::streamsize size = file.tellg();
    const std::streamsize chunk_size = 10 * 1024;
    std::streamsize read_size = std::min(size, chunk_size);

    file.seekg(size - read_size, std::ios::beg);

    std::string buffer(read_size, '\0');
    file.read(&buffer[0], read_size);
    file.close();

    // Find first '\n' or '\r\n' to skip partial line
    size_t first_newline = buffer.find('\n');
    if (first_newline != std::string::npos) {
        // Trim everything *up to and including* the newline
        return buffer.substr(first_newline + 1);
    }

    // No newline found — return the buffer as is
    return buffer;
}

THTTPStatus LogPageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties)
{
    LOGNOTE("Log page called");
    
    context["log_lines"] = read_loglines("/usbode-logs.txt");
    
    return HTTPOK;
}
