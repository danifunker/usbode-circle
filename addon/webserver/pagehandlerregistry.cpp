// PageHandlerRegistry.cpp
#include "pagehandlerregistry.h"
#include "handlers/notfound.h"
#include "handlers/homepage.h"
#include "handlers/mountpage.h"

// ----------------------------------------------------------------------
// Define and initialize the static array of PageHandlerEntry structs.
// ----------------------------------------------------------------------
static HomePageHandler s_homePageHandler;
static NotFoundPageHandler s_notFoundPageHandler;
static MountPageHandler s_mountPageHandler;

const PageHandlerEntry PageHandlerRegistry::s_pathHandlers[] = {
    {"/",        &s_homePageHandler},
    {"/mount",        &s_mountPageHandler}
};

// Define the size of the static array.
// This is calculated automatically at compile time.
const int PageHandlerRegistry::s_numHandlers =
    sizeof(PageHandlerRegistry::s_pathHandlers) / sizeof(PageHandlerEntry);


// ----------------------------------------------------------------------
// Implement the getHandler method.
// This now manually iterates through the array to find a match.
// ----------------------------------------------------------------------
IPageHandler* PageHandlerRegistry::getHandler(const char* path) {
    for (int i = 0; i < s_numHandlers; ++i) {
        // Use strcmp for C-style string comparison
        if (strcmp(path, s_pathHandlers[i].path) == 0) {
            return s_pathHandlers[i].handler;
        }
    }
    // If no specific handler is found, return the default "not found" handler
    return &s_notFoundPageHandler;
}
