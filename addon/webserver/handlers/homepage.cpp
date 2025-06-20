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
#include "util.h"

using namespace kainjow;

LOGMODULE("homepagehandler");

char s_Index[] =
#include "index.h"
;

#define ITEMS_PER_PAGE 35  // Number of items per page

std::string HomePageHandler::GetHTML() {
        return std::string(s_Index);
}

THTTPStatus HomePageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties,
                                   CUSBCDGadget *pCDGadget)
{
        LOGDBG("Home page called");

	// Ask scsitbservice for a list of filenames
	SCSITBService* svc = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
	if (!svc)
            return HTTPInternalServerError;

	// Get current loaded image
	std::string current_image = svc->GetCurrentCDName();
	context.set("image_name", current_image);

	std::vector<kainjow::mustache::data> all_links_vec;

	for (const FileEntry* it = svc->begin(); it != svc->end(); ++it) {

            std::string full_name(it->name);

            //LOGDBG("Read directory index %s", full_name.c_str());

            // Define the display name
            size_t dot_pos = full_name.rfind('.');
            std::string display_name;
            if (dot_pos != std::string::npos) {
                display_name = full_name.substr(0, dot_pos);
            } else {
                display_name = full_name;
            }

            std::string current = "";
            std::string style = "";
            if (full_name == current_image) {
                current = " (Current)";
                style = " style=\"font-weight:bold;border:2px solid #4CAF50;\"";
            }

            mustache::data link;
            link.set("display_name", display_name);
            link.set("file_name", full_name);
            link.set("current", current);
            link.set("style", style);
            all_links_vec.push_back(link);
        }

        // Get the requested page number from parameters
        int page = 1;
	auto params = parse_query_params(pParams);
	auto it = params.find("page");
	if (it != params.end()) {
	    try {
		int parsed = std::stoi(it->second);
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
        
        // Find page with current image
        int current_image_page = 0;
        for (size_t i = 0; i < all_links_vec.size(); i++) {
            auto link = all_links_vec[i];
            const auto* file_name_ptr = link.get("file_name");
            if (file_name_ptr && file_name_ptr->string_value() == current_image) {
                current_image_page = (i / ITEMS_PER_PAGE) + 1;
                break;
            }
        }
        
        // If no page specified and we found current image, go to that page
	// TODO: If the found image is not on page 1, we should redirect the browser
	// to a different page so that the query param in the url reflects the page
	// we want to be on. However, need to implement a way to return a redirect
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
        
        // Only add pagination if we have more than one page
        if (total_pages > 1) {
            mustache::data pagination;
            
            // Add page numbers as strings
            char page_str[16], total_pages_str[16];
            snprintf(page_str, sizeof(page_str), "%d", page);
            snprintf(total_pages_str, sizeof(total_pages_str), "%d", total_pages);
            
            pagination.set("current_page", page_str);
            pagination.set("total_pages", total_pages_str);
            
            // First page link
            pagination.set("has_first", page > 1);
            
            // Previous page link (only if not first page and not same as first)
            pagination.set("has_prev", page > 2);
            if (page > 2) {
                char prev_str[16];
                snprintf(prev_str, sizeof(prev_str), "%d", page - 1);
                pagination.set("prev_page", prev_str);
            }
            
            // Next page link (only if not last page and not same as last)
            pagination.set("has_next", page < total_pages - 1);
            if (page < total_pages - 1) {
                char next_str[16];
                snprintf(next_str, sizeof(next_str), "%d", page + 1);
                pagination.set("next_page", next_str);
            }
            
            // Last page link
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
