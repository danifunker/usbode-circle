#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <mustache/mustache.hpp>
#include <circle/koptions.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <algorithm>
#include <gitinfo/gitinfo.h>
#include "homepage.h"

using namespace kainjow;

LOGMODULE("homepagehandler");

char s_Index[] =
#include "index.h"
;

#define VERSION "2.1.0"

void sort_links_by_display_name(std::vector<kainjow::mustache::data>& links_vec) {
    std::sort(links_vec.begin(), links_vec.end(), [](const auto& a, const auto& b) {
        const auto* a_name_ptr = a.get("display_name");
        const auto* b_name_ptr = b.get("display_name");

        const std::string& a_name = a_name_ptr ? a_name_ptr->string_value() : "";
        const std::string& b_name = b_name_ptr ? b_name_ptr->string_value() : "";

        return a_name < b_name;
    });
}

THTTPStatus HomePageHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *pCDGadget)
{
	LOGNOTE("Home page called");

	// Set up Mustache Template Engine
	mustache::mustache tmpl{s_Index};
	mustache::data context;


	// Get current loaded image
	m_pProperties->Load();
	m_pProperties->SelectSection("usbode");
	std::string current_image = m_pProperties->GetString("current_image", "image.iso");
	context.set("current_image", current_image);


	// Open directory
	DIR dir;
	FILINFO fno;
	std::vector<kainjow::mustache::data> links_vec;

	FRESULT fr = f_opendir(&dir, "/images");
	if (fr != FR_OK)
		return HTTPInternalServerError;

	while (1) {
	    fr = f_readdir(&dir, &fno);
	    if (fr != FR_OK || fno.fname[0] == 0)
		break;

	    std::string full_name(fno.fname);

	    if (full_name == "." || full_name == ".." ||
		    (full_name.size() >= 4 && full_name.substr(full_name.size() - 4) == ".cue")) {
		    continue;
		}

	    LOGNOTE("Read directory index %s", full_name.c_str());

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
	    link.set("display_name", display_name); //TODO remove file extension
	    link.set("file_name", full_name);
	    link.set("current", current);
	    link.set("style", style);
	    links_vec.push_back(link);
	}
        f_closedir(&dir);

	// Sort the links
	sort_links_by_display_name(links_vec);

	// Add to our Mustache context
	mustache::data links{mustache::data::type::list};
	for (auto& link : links_vec) {
	    links.push_back(link);
	}
	context.set("links", links);

	// Find the current USB mode
	// TODO replace with Property get if we move the location of this
	boolean is_full_speed = CKernelOptions::Get()->GetUSBFullSpeed();
	if (is_full_speed)
		context.set("usb_mode", "FullSpeed");
	else
		context.set("usb_mode", "HighSpeed");
	
	// Add build info
	context.set("version", VERSION);
	context.set("build_info", std::string(GIT_BRANCH) + " @ " + std::string(GIT_COMMIT) + " | " + __DATE__ + " " + __TIME__);

	// Render
	LOGNOTE("Rendering the template");
	std::string rendered = tmpl.render(context);

	if (pBuffer && *pLength >= rendered.length()) {
		memcpy(pBuffer, rendered.c_str(), rendered.length());
		*pLength = rendered.length();
		*ppContentType = "text/html"; // Most likely HTML for a templating engine
		return HTTPOK;
	}
	
	// The provided buffer is too small
	LOGERR("Output buffer too small for rendered content.");
	*pLength = 0; // Indicate no content could be written
	*ppContentType = "text/plain"; // Default content type for error
	return HTTPInternalServerError; // Or a more specific error
}
