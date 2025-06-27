// PageHandlerRegistry.h
#ifndef PAGE_HANDLER_REGISTRY_H
#define PAGE_HANDLER_REGISTRY_H

#include <circle/net/httpdaemon.h>
#include <circle/util.h>

#include "handlers/pagehandler.h"

// Structure to hold a path-handler pair
// This replaces the role of a std::map entry
struct PageHandlerEntry {
    const char* path;
    IPageHandler* handler;
};

class PageHandlerRegistry {
public:
    static IPageHandler* getHandler(const char* path);

private:
    static const PageHandlerEntry s_pathHandlers[];
    static const int s_numHandlers;
};

#endif // PAGE_HANDLER_REGISTRY_H
