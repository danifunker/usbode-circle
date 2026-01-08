#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <scsitbservice/scsitbservice.h>
#include <circle/koptions.h>
#include <vector>
#include <string>
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
    if (path.empty()) return "";

    // Remove trailing slash if present
    std::string p = path;
    if (!p.empty() && p.back() == '/')
        p.pop_back();

    // Find last slash
    size_t lastSlash = p.rfind('/');
    if (lastSlash == std::string::npos)
        return "";  // No parent, we're at root level

    return p.substr(0, lastSlash + 1);  // Include trailing slash
}

THTTPStatus HomePageHandler::PopulateContext(kainjow::mustache::data& context,
        const char *pPath,
        const char  *pParams,
        const char  *pFormData)
{
    LOGDBG("Home page called");

    SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    if (!svc)
        return HTTPInternalServerError;

    auto params = parse_query_params(pParams);

    // Get current browse path from ?path= parameter
    std::string current_path = "";
    auto path_it = params.find("path");
    if (path_it != params.end()) {
        current_path = path_it->second;
        // Ensure trailing slash for non-empty paths
        if (!current_path.empty() && current_path.back() != '/')
            current_path += '/';
    }

    // Refresh cache for current path
    svc->RefreshCacheForPath(current_path.c_str());

    // Set path-related context variables
    bool is_root = current_path.empty();
    context.set("current_path", current_path);
    context.set("is_root", is_root);
    context.set("show_path", !is_root);

    // Compute parent path for ".." navigation
    std::string parent_path = get_parent_path(current_path);
    context.set("parent_path", parent_path);

    // Get current loaded image info
    const char* current_image_path = svc->GetCurrentCDPath();
    std::string current_image_name = "";
    if (current_image_path && current_image_path[0] != '\0') {
        // Extract just filename from path for display
        const char* lastSlash = strrchr(current_image_path, '/');
        current_image_name = lastSlash ? (lastSlash + 1) : current_image_path;
    }
    context.set("image_name", current_image_name);
    context.set("image_path", current_image_path ? current_image_path : "");

    // Build all links with folder support
    std::vector<kainjow::mustache::data> all_links_vec;

    for (const FileEntry* entry = svc->begin(); entry != svc->end(); ++entry) {
        mustache::data link;
        std::string name(entry->name);
        link.set("file_name", name);
        link.set("is_folder", entry->isDirectory);

        if (entry->isDirectory) {
            // Folder: link to /?path=current_path/folder_name
            std::string folder_path = current_path + name + "/";
            link.set("folder_path", folder_path);
            link.set("style", " folder");
            link.set("current", "");
        } else {
            // File: check if it's the currently mounted image
            std::string file_path = current_path + name;
            std::string full_path = "1:/" + file_path;

            std::string current_marker = "";
            std::string style = "";
            if (current_image_path && full_path == current_image_path) {
                current_marker = " (Current)";
                style = " current";
            }

            link.set("current", current_marker);
            link.set("style", style);

            // URL-encode the path for mount link
            link.set("file_path", file_path);
            link.set("file_path_encoded", url_encode_path(file_path));
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

    // Find page with current image (only if we're at root or in the same folder)
    int current_image_page = 0;
    for (size_t i = 0; i < all_links_vec.size(); i++) {
        auto& link = all_links_vec[i];
        const auto* current_ptr = link.get("current");
        if (current_ptr && !current_ptr->string_value().empty()) {
            current_image_page = (i / ITEMS_PER_PAGE) + 1;
            break;
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
        pagination.set("path_param", current_path.empty() ? "" : "&path=" + url_encode_path(current_path));

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

    return HTTPOK;
}
