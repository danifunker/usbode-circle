#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <scsitbservice/scsitbservice.h>
#include <configservice/configservice.h>
#include <circle/koptions.h>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <gitinfo/gitinfo.h>
#include "homepage.h"
#include "../util.h"

using namespace kainjow;

LOGMODULE("homepagehandler");

char s_Index[] =
#include "index.h"
;

#define ITEMS_PER_PAGE 35  // Number of items per page

std::string HomePageHandler::GetHTML()
{
    return std::string(s_Index);
}

// Helper to URL-encode a string (encode special chars for query params)
static std::string url_encode_path(const std::string& value) {
    std::string result;
    for (char c : value) {
        if (c == '/') {
            result += "%2F";
        } else if (c == ' ') {
            result += "%20";
        } else if (c == '&') {
            result += "%26";
        } else if (c == '?') {
            result += "%3F";
        } else if (c == '=') {
            result += "%3D";
        } else {
            result += c;
        }
    }
    return result;
}

// Helper to compute parent path from current path
static std::string get_parent_path(const std::string& path) {
    if (path.empty()) return "/";  // If at root, parent is still root

    // Remove trailing slash if present
    std::string p = path;
    if (!p.empty() && p.back() == '/')
        p.pop_back();

    // Find last slash
    size_t lastSlash = p.rfind('/');
    if (lastSlash == std::string::npos)
        return "/";  // No parent, go to root

    if (lastSlash == 0)
        return "/";  // Parent is root

    return p.substr(0, lastSlash);  // Return parent without trailing slash
}

THTTPStatus HomePageHandler::PopulateContext(kainjow::mustache::data& context,
        const char *pPath,
        const char  *pParams,
        const char  *pFormData)
{
    LOGNOTE("HomePageHandler::PopulateContext called");

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc) {
        LOGERR("HomePageHandler: scsitbservice is null!");
        return HTTPInternalServerError;
    }

    ConfigService* config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
    bool flatFileList = config ? config->GetFlatFileList() : false;

    auto params = parse_query_params(pParams);

    // Get current loaded image info first
    const char* current_image_path = svc->GetCurrentCDPath();
    std::string current_image_folder = "";
    
    if (current_image_path && current_image_path[0] != '\0') {
        // Extract folder from current image path (e.g., "1:/Games/RPG/game.iso" -> "Games/RPG")
        const char* pathStart = current_image_path;
        if (strncmp(pathStart, "1:/", 3) == 0) {
            pathStart += 3;  // Skip "1:/"
        }
        
        const char* lastSlash = strrchr(pathStart, '/');
        if (lastSlash != nullptr) {
            // Has folder component
            current_image_folder = std::string(pathStart, lastSlash - pathStart);
        }
    }

    // Get current browse path from ?path= parameter
    std::string current_path = "";
    auto path_it = params.find("path");
    if (path_it != params.end()) {
        current_path = path_it->second;
        LOGNOTE("HomePageHandler: path parameter = '%s'", current_path.c_str());
        // Normalize: remove trailing slash
        while (!current_path.empty() && current_path.back() == '/')
            current_path.pop_back();
    } else if (!current_image_folder.empty()) {
        // No path parameter provided - auto-navigate to folder with current image
        current_path = current_image_folder;
        LOGNOTE("HomePageHandler: Auto-navigating to current image folder: '%s'", current_path.c_str());
    }

    LOGNOTE("HomePageHandler: Filtering entries for path='%s'", current_path.c_str());

    bool isRoot = current_path.empty();
    size_t pathLen = current_path.length();

    // Set path-related context variables
    bool is_root = current_path.empty();
    context.set("current_path", current_path);
    context.set("is_root", is_root);
    context.set("show_path", !is_root && !flatFileList);
    context.set("flat_file_list", flatFileList);

    // Compute parent path for ".." navigation
    std::string parent_path = get_parent_path(current_path);
    context.set("parent_path", parent_path);

    // Get current loaded image display name
    std::string current_image_name = "";
    if (current_image_path && current_image_path[0] != '\0') {
        const char* lastSlash = strrchr(current_image_path, '/');
        current_image_name = lastSlash ? (lastSlash + 1) : current_image_path;
    }
    
    context.set("image_name", current_image_name);
    context.set("image_path", current_image_path ? current_image_path : "");
    
    // Check if we're browsing the folder that contains the current image
    bool browsing_current_folder = (current_path == current_image_folder);
    
    LOGNOTE("HomePageHandler: current_image_folder='%s', browsing_current_folder=%d", 
            current_image_folder.c_str(), browsing_current_folder);

    // Build all links with folder support
    LOGNOTE("HomePageHandler: Building links for path='%s'", current_path.c_str());
    std::vector<kainjow::mustache::data> all_links_vec;

    // Iterate through all cached entries and filter
    for (const FileEntry* entry = svc->begin(); entry != svc->end(); ++entry) {
        const char* entryPath = entry->relativePath;

        // Filter logic: show only entries at current depth (unless flat mode is enabled)
        bool showEntry = false;
        if (flatFileList) {
            // Flat mode: show all files (skip directories)
            showEntry = !entry->isDirectory;
        } else if (isRoot) {
            // Root: show entries with no '/' in their path
            showEntry = (strchr(entryPath, '/') == nullptr);
        } else {
            // Subfolder: show entries that start with "path/" and have no additional '/'
            if (strncmp(entryPath, current_path.c_str(), pathLen) == 0 && entryPath[pathLen] == '/') {
                const char* remainder = entryPath + pathLen + 1;
                showEntry = (strchr(remainder, '/') == nullptr);
            }
        }

        if (!showEntry)
            continue;

        mustache::data link;
        std::string name(entry->name);
        link.set("file_name", name);
        link.set("is_folder", entry->isDirectory);
        link.set("flat_display_path", flatFileList);

        if (entry->isDirectory) {
            // Folder: link to /?path=relativePath
            link.set("folder_path", std::string(entry->relativePath));
            link.set("style", " folder");
            link.set("current", "");
        } else {
            // File: check if it's the currently mounted image (only if we're in the same folder)
            std::string full_path = "1:/" + std::string(entry->relativePath);

            std::string current_marker = "";
            std::string style = "";
            if (browsing_current_folder && current_image_path && full_path == current_image_path) {
                current_marker = " (Current)";
                style = " current";
            }

            link.set("current", current_marker);
            link.set("style", style);

            // URL-encode the relative path for mount link
            link.set("file_path", std::string(entry->relativePath));
            link.set("file_path_encoded", url_encode_path(entry->relativePath));
        }

        all_links_vec.push_back(link);
    }

    // Get the requested page number from parameters
    int page = 1;
    auto page_it = params.find("page");
    if (page_it != params.end()) {
        try {
            int parsed = std::stoi(page_it->second);
            if (parsed > 0) {
                page = parsed;
            }
        } catch (const std::exception&) {
            // Ignore invalid input, use default
        }
    }

    // Calculate total pages and ensure page is valid
    int total_items = all_links_vec.size();
    int total_pages = (total_items + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;

    if (total_pages == 0) total_pages = 1;
    if (page > total_pages) page = total_pages;

    // Build path parameter string for pagination links
    std::string path_param = "";
    if (!current_path.empty()) {
        path_param = "&path=" + url_encode_path(current_path);
    }

    // Find page with current image (only if we're browsing the folder containing it)
    int current_image_page = 0;
    if (browsing_current_folder) {
        for (size_t i = 0; i < all_links_vec.size(); i++) {
            auto& link = all_links_vec[i];
            const auto* current_ptr = link.get("current");
            if (current_ptr && !current_ptr->string_value().empty()) {
                current_image_page = (i / ITEMS_PER_PAGE) + 1;
                LOGNOTE("HomePageHandler: Found current image at index %d, page %d", (int)i, current_image_page);
                break;
            }
        }
    }

    // If no page specified and we found current image, go to that page
    if (params.find("page") == params.end()) {
        if (current_image_page > 0) {
            page = current_image_page;
        }
    }

    // Get subset of links for current page
    int start_idx = (page - 1) * ITEMS_PER_PAGE;
    int end_idx = start_idx + ITEMS_PER_PAGE;
    if (end_idx > total_items) end_idx = total_items;

    // Create list of current page links
    mustache::data links{mustache::data::type::list};
    for (int i = start_idx; i < end_idx; i++) {
        links.push_back(all_links_vec[i]);
    }
    context.set("links", links);

    // Pagination - include path in page links
    if (total_pages > 1) {
        mustache::data pagination;

        char page_str[16], total_pages_str[16];
        snprintf(page_str, sizeof(page_str), "%d", page);
        snprintf(total_pages_str, sizeof(total_pages_str), "%d", total_pages);

        pagination.set("current_page", page_str);
        pagination.set("total_pages", total_pages_str);
        
        // Always include path parameter (use "/" for root)
        std::string path_for_url = current_path.empty() ? "/" : current_path;
        pagination.set("path_param", "&path=" + url_encode_path(path_for_url));

        pagination.set("has_first", page > 1);

        pagination.set("has_prev", page > 2);
        if (page > 2) {
            char prev_str[16];
            snprintf(prev_str, sizeof(prev_str), "%d", page - 1);
            pagination.set("prev_page", prev_str);
        }

        pagination.set("has_next", page < total_pages - 1);
        if (page < total_pages - 1) {
            char next_str[16];
            snprintf(next_str, sizeof(next_str), "%d", page + 1);
            pagination.set("next_page", next_str);
        }

        pagination.set("has_last", page < total_pages);
        if (page < total_pages) {
            char last_str[16];
            snprintf(last_str, sizeof(last_str), "%d", total_pages);
            pagination.set("last_page", last_str);
        }

        context.set("pagination", pagination);
    }

    LOGNOTE("HomePageHandler: PopulateContext completed successfully");
    return HTTPOK;
}
