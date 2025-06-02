//
// webserver.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "webserver.h"

#include <assert.h>
#include <circle/logger.h>
#include <circle/new.h>
#include <circle/string.h>

#include "util.h"

#define MAX_CONTENT_SIZE 16384
#define MAX_FILES 1024
#define MAX_FILES_PER_PAGE 50
#define MAX_FILENAME 255
#define VERSION "2.0.1"
#define DRIVE "SD:"
#define CONFIG_FILE DRIVE "/config.txt"

// HTML template with CSS styling embedded
static const char HTML_LAYOUT[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title>USBODE - USB Optical Drive Emulator</title>\n"
    "    <style>\n"
    "        body {background-color: #EAEAEA; color: #333333; font-family: \"Times New Roman\", serif; margin: 0; padding: 0;}\n"
    "        h1, h2, h3 {color: #1E4D8C;}\n"
    "        a {color: #0066CC;}\n"
    "        a:visited {color: #0066CC;}\n"
    "        .container {width: 100%%; margin: 0; padding: 0;}\n"
    "        .header {background-color: #3A7CA5; padding: 10px; text-align: center; color: #FFFFFF;}\n"
    "        .header h1, .header h2 {color: #FFFFFF; margin: 5px 0;}\n"
    "        .content {padding: 10px; background-color: #FFFFFF; min-height: 300px;}\n"
    "        .footer {background-color: #3A7CA5; padding: 10px; text-align: center; color: #FFFFFF;}\n"
    "        .button {background-color: #4CAF50; padding: 7px 15px; text-decoration: none; color: #FFFFFF; margin: 5px; display: inline-block;}\n"
    "        .info-box {background-color: #F5F5F5; padding: 10px; margin: 10px 0;}\n"
    "        .warning {background-color: #FFDDDD; padding: 10px; margin: 10px 0; color: #990000;}\n"
    "        .file-link {padding: 8px; margin: 5px 0; display: block; font-size: 16px;}\n"
    "        .file-link-even {background-color: #E3F2FD;}\n"
    "        .file-link-odd {background-color: #BBDEFB;}\n"
    "        .header-bar {background-color: #2C5F7C; color: #FFFFFF; padding: 5px;}\n"
    "        .usb-info {background-color: #E3F2FD; border-top: 1px solid #BBDEFB; padding: 5px; text-align: center; margin-top: 20px;}\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <div class=\"header\">\n"
    "            <h1>USBODE</h1>\n"
    "            <h2>USB Optical Drive Emulator</h2>\n"
    "        </div>\n"
    "        <div class=\"content\">\n"
    "            %s\n"
    "        </div>\n"
    "        <div class=\"usb-info\">\n"
    "            <p>USB Mode: %s</p>\n"
    "        </div>\n"
    "        <div class=\"footer\">\n"
    "            <p>Version %s</p>\n"
    "        </div>\n"
    "    </div>\n"
    "</body>\n"
    "</html>";

static const u8 s_Index[] =
#include "index.h"
    ;

LOGMODULE("webserver");

TShutdownMode CWebServer::s_GlobalShutdownMode = ShutdownNone;

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CUSBCDGadget *pCDGadget, CActLED *pActLED, CPropertiesFatFsFile *pProperties, CSocket *pSocket)
:       CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE),
        m_pActLED (pActLED),
        m_pCDGadget (pCDGadget),
        m_pContentBuffer(new u8[MAX_CONTENT_SIZE]),
        m_pProperties(pProperties),
        m_ShutdownMode(ShutdownNone),
        m_pDisplayUpdateHandler(nullptr) // Initialize the display update handler
{
    // Select the correct section for all property operations
    m_pProperties->SelectSection("usbode");
}

CWebServer::~CWebServer (void)
{
        m_pActLED = 0;
        delete[] m_pContentBuffer;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
        return new CWebServer (pNetSubSystem, m_pCDGadget, m_pActLED, m_pProperties, pSocket);
}

THTTPStatus CWebServer::list_files_as_table(char *output_buffer, size_t max_len, const char *params, const char *pUSBSpeed) 
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    char *content = new (HEAP_LOW) char[MAX_CONTENT_SIZE];

    size_t offset = 0;

    // Set pagination constants - reduce to avoid stack overflow
    const int FILES_PER_PAGE = 25;  // Reduced from 50
    int current_page = 1;
    int total_pages = 1;
    int total_files = 0;
    int current_file_index = -1;
    int page_with_current_file = 0;

    // Parse page parameter if exists
    if (params != NULL) {
        const char *page_param = strstr(params, "page=");
        if (page_param) {
            current_page = atoi(page_param + 5);  // Skip "page="
            if (current_page < 1) current_page = 1;
        }
    }
    
    // Make sure we're using the latest data from properties file
    m_pProperties->Load();
    m_pProperties->SelectSection("usbode");
    
    // Get current mounted image name (defaulting if necessary);
    const char *currentImage = m_pProperties->GetString("current_image", "image.iso");

    // Add header to content with safe checks - REMOVED SHUTDOWN BUTTON FROM HERE
    offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                       "<h3>File Selection</h3>\n"
                       "<div class=\"info-box\">\n"
                       "    <p>Current File Loaded: <strong>%s</strong></p>\n"
                       "</div>\n",
                       currentImage);

    // Open directory
    fr = f_opendir(&dir, "/images");
    if (fr != FR_OK) {
        offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                           "<p>Error opening directory: %d</p>", fr);
    } else {
        // We'll collect all filenames first, sort them, then display the appropriate page
        char **filenames = NULL;
        int allocated_files = 0;

        // First pass: count total files
        total_files = 0;

        while (1) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) break;

            // Skip "." and ".."
            if (fno.fname[0] == '.' && (fno.fname[1] == 0 || (fno.fname[1] == '.' && fno.fname[2] == 0))) {
                continue;
            }

            total_files++;
        }

        f_closedir(&dir);

        // Allocate memory for filenames only if we have files
        if (total_files > 0) {
            filenames = new char *[total_files];
            if (filenames) {
                // Initialize pointers to NULL for safe cleanup later
                for (int i = 0; i < total_files; i++) {
                    filenames[i] = NULL;
                }

                allocated_files = total_files;

                // Re-read directory and collect filenames
                fr = f_opendir(&dir, "/images");
                if (fr == FR_OK) {
                    int file_index = 0;

                    while (file_index < total_files) {
                        fr = f_readdir(&dir, &fno);
                        if (fr != FR_OK || fno.fname[0] == 0) break;

                        // Skip "." and ".."
                        if (fno.fname[0] == '.' && (fno.fname[1] == 0 || (fno.fname[1] == '.' && fno.fname[2] == 0))) {
                            continue;
                        }

                        // Allocate memory for this filename
                        size_t name_len = strlen(fno.fname);
                        filenames[file_index] = new char[name_len + 1];

                        if (filenames[file_index]) {
                            // Copy the filename
                            strncpy(filenames[file_index], fno.fname, name_len);
                            filenames[file_index][name_len] = '\0';

                            // Check if this is the current file
                            if (strcmp(fno.fname, currentImage) == 0) {
                                current_file_index = file_index;
                            }

                            file_index++;
                        }
                    }

                    f_closedir(&dir);

                    // Now sort all filenames using bubble sort
                    for (int i = 0; i < file_index - 1; i++) {
                        for (int j = 0; j < file_index - i - 1; j++) {
                            if (strcasecmp(filenames[j], filenames[j + 1]) > 0) {
                                // Swap pointers
                                char *temp = filenames[j];
                                filenames[j] = filenames[j + 1];
                                filenames[j + 1] = temp;

                                // Update current_file_index if needed
                                if (current_file_index == j) {
                                    current_file_index = j + 1;
                                } else if (current_file_index == j + 1) {
                                    current_file_index = j;
                                }
                            }
                        }
                    }

                    // Now find where the current file is in the sorted list
                    if (current_file_index >= 0) {
                        page_with_current_file = (current_file_index / FILES_PER_PAGE) + 1;
                    }
                }
            }
        }

        // Calculate total pages
        total_pages = (total_files + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
        if (total_pages < 1) total_pages = 1;

        // If we didn't specify a page and found the current file, go to its page
        if ((params == NULL || strstr(params, "page=") == NULL) && page_with_current_file > 0) {
            current_page = page_with_current_file;
        }

        // Ensure current_page is in valid range
        if (current_page > total_pages) current_page = total_pages;
        if (current_page < 1) current_page = 1;

        offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                           "<h4>Available Files (Page %d of %d):</h4>\n",
                           current_page, total_pages);

        // Display the sorted files for the current page
        if (filenames && allocated_files > 0) {
            // Calculate start and end indices for current page
            int start_index = (current_page - 1) * FILES_PER_PAGE;
            int end_index = start_index + FILES_PER_PAGE;
            if (end_index > total_files) end_index = total_files;

            // Still process in small chunks to avoid memory issues
            const int CHUNK_SIZE = 10;
            int row_index = 0;

            for (int chunk_start = start_index; chunk_start < end_index; chunk_start += CHUNK_SIZE) {
                int chunk_end = chunk_start + CHUNK_SIZE;
                if (chunk_end > end_index) chunk_end = end_index;

                // Display files in this chunk
                for (int i = chunk_start; i < chunk_end; i++) {
                    if (i < allocated_files && filenames[i]) {
                        // Check remaining space before adding a new file entry
                        size_t entry_size = strlen(filenames[i]) * 2 + 200;  // conservative estimate
                        if (MAX_CONTENT_SIZE - offset <= entry_size) {
                            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                               "<p>Too many files to display completely</p>");
                            break;
                        }

                        // Add file with alternating row colors and highlight current
                        const char *rowClass = (row_index % 2 == 0) ? "file-link-even" : "file-link-odd";
                        row_index++;

                        // Check if this is the currently loaded file
                        bool is_current = (i == current_file_index);

                        if (is_current) {
                            // Highlight the current file
                            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                               "<div class=\"file-link %s\" style=\"font-weight:bold;border:2px solid #4CAF50;\">"
                                               "<a href=\"/mount?file=%s\">%s</a> (Current)</div>\n",
                                               rowClass, filenames[i], filenames[i]);
                        } else {
                            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                               "<div class=\"file-link %s\"><a href=\"/mount?file=%s\">%s</a></div>\n",
                                               rowClass, filenames[i], filenames[i]);
                        }
                    }
                }
            }

            // Clean up allocated memory
            for (int i = 0; i < allocated_files; i++) {
                if (filenames[i]) {
                    delete[] filenames[i];
                }
            }
            delete[] filenames;
        }

        // Add pagination controls with simplified layout
        if (MAX_CONTENT_SIZE - offset > 300) {
            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                               "<div style=\"margin-top: 20px; text-align: center;\">\n");

            // Previous page button
            if (current_page > 1) {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=%d\">&laquo; Previous</a>\n", current_page - 1);
            } else {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<span class=\"button\" style=\"opacity: 0.5;\">&laquo; Previous</span>\n");
            }

            // Simplified page navigation - just show first, current-1, current, current+1, last
            if (current_page > 2) {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=1\">1</a>\n");

                if (current_page > 3) {
                    offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                       "<span style=\"margin: 0 5px;\">...</span>\n");
                }
            }

            if (current_page > 1) {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=%d\">%d</a>\n",
                                   current_page - 1, current_page - 1);
            }

            // Current page is highlighted
            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                               "<span class=\"button\" style=\"background-color:#1E4D8C;\">%d</span>\n", current_page);

            if (current_page < total_pages) {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=%d\">%d</a>\n",
                                   current_page + 1, current_page + 1);
            }

            if (current_page < total_pages - 1) {
                if (current_page < total_pages - 2) {
                    offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                       "<span style=\"margin: 0 5px;\">...</span>\n");
                }

                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=%d\">%d</a>\n",
                                   total_pages, total_pages);
            }

            // Next page button
            if (current_page < total_pages) {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<a class=\"button\" href=\"/list?page=%d\">Next &raquo;</a>\n", current_page + 1);
            } else {
                offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                                   "<span class=\"button\" style=\"opacity: 0.5;\">Next &raquo;</span>\n");
            }

            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1, "</div>\n");

            // ADD SHUTDOWN BUTTON HERE - after pagination
            offset += snprintf(content + offset, MAX_CONTENT_SIZE - offset - 1,
                               "<div style=\"margin-top: 20px; text-align: center;\">\n"
                               "    <a class=\"button\" href=\"/system?action=shutdown\">Shutdown USBODE</a>\n"
                               "</div>\n");
        }
    }

    // Format the complete HTML page using the layout template
    snprintf(output_buffer, max_len, HTML_LAYOUT, content, pUSBSpeed, VERSION);
    delete[] content;
    return HTTPOK;
}

THTTPStatus CWebServer::list_files_as_json(char *json_output, size_t max_len) 
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    size_t offset = 0;

    // For filename sorting
    char filenames[MAX_FILES][MAX_FILENAME + 1];
    int total_files = 0;

    fr = f_opendir(&dir, "/images");
    if (fr != FR_OK) {
        snprintf(json_output, max_len, "{\"error\": %d}", fr);
        return HTTPInternalServerError;
    }

    // First pass: collect all filenames
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        // Skip "." and ".."
        if (fno.fname[0] == '.' && (fno.fname[1] == 0 || (fno.fname[1] == '.' && fno.fname[2] == 0))) {
            continue;
        }

        // Check if we have room for more files
        if (total_files >= MAX_FILES) {
            LOGERR("Too many files, increase MAX_FILES");
            break;
        }

        // Store the filename
        strncpy(filenames[total_files], fno.fname, MAX_FILENAME);
        filenames[total_files][MAX_FILENAME] = '\0';
        total_files++;
    }

    f_closedir(&dir);

    // Sort the filenames (simple bubble sort)
    for (int i = 0; i < total_files - 1; i++) {
        for (int j = 0; j < total_files - i - 1; j++) {
            if (strcasecmp(filenames[j], filenames[j + 1]) > 0) {
                // Swap filenames
                char temp[MAX_FILENAME + 1];
                strncpy(temp, filenames[j], MAX_FILENAME);
                temp[MAX_FILENAME] = '\0';

                strncpy(filenames[j], filenames[j + 1], MAX_FILENAME);
                filenames[j][MAX_FILENAME] = '\0';

                strncpy(filenames[j + 1], temp, MAX_FILENAME);
                filenames[j + 1][MAX_FILENAME] = '\0';
            }
        }
    }

    offset += snprintf(json_output + offset, max_len - offset, "[");

    // Add sorted filenames to JSON array
    for (int i = 0; i < total_files; i++) {
        // Add comma separator if not the first item
        if (i > 0) {
            offset += snprintf(json_output + offset, max_len - offset, ",");
        }

        // Add filename to JSON array
        offset += snprintf(json_output + offset, max_len - offset, "\"%s\"", filenames[i]);

        if (offset >= max_len - MAX_FILENAME - 4) {  // prevent overflow
            break;
        }
    }

    snprintf(json_output + offset, max_len - offset, "]");
    return HTTPOK;
}

THTTPStatus CWebServer::generate_mount_success_page(char *output_buffer, size_t max_len, const char *filename, const char *pUSBSpeed) 
{
    const char* html = 
        "<h3>Mounting File</h3>\n"
        "<div class=\"info-box\">\n"
        "    <p>Successfully mounted: <strong>%s</strong></p>\n"
        "</div>\n"
        "\n"
        "<div>\n"
        "    <a class=\"button\" href=\"/list\">Return to File List</a>\n"
        "</div>";
    size_t content_buffer_size = strlen(html) + MAX_FILENAME + 1;
    char *content = new (HEAP_LOW) char[content_buffer_size];

    snprintf(content, content_buffer_size,
             html,
             filename);

    // Format the complete HTML page using the layout template
    snprintf(output_buffer, max_len, HTML_LAYOUT, content, pUSBSpeed, VERSION);
    delete[] content;  // Fixed: Use delete[] for array allocation
    return HTTPOK;
}

THTTPStatus CWebServer::handle_system_operation(char *content, size_t max_len, const char *action, TShutdownMode *pShutdownMode, const char *pUSBSpeed) {
    
    if (strcmp(action, "shutdown") == 0) {
        snprintf(content, MAX_CONTENT_SIZE - 1, HTML_LAYOUT,
            "<h3>System Shutdown</h3>\n"
            "<div class=\"info-box\">\n"
            "    <p>The system is shutting down...</p>\n"
            "</div>", 
            pUSBSpeed, VERSION);
        
        // Set the global shutdown mode instead of the instance variable
        CWebServer::SetGlobalShutdownMode(ShutdownHalt);
        *pShutdownMode = ShutdownHalt;  // Also set the passed pointer for compatibility
    } 
    else if (strcmp(action, "reboot") == 0) {
        snprintf(content, MAX_CONTENT_SIZE - 1, HTML_LAYOUT,
            "<h3>System Reboot</h3>\n"
            "<div class=\"info-box\">\n"
            "    <p>The system is rebooting...</p>\n"
            "</div>", 
            pUSBSpeed, VERSION);
        
        // Set the global shutdown mode instead of the instance variable
        CWebServer::SetGlobalShutdownMode(ShutdownReboot);
        *pShutdownMode = ShutdownReboot;  // Also set the passed pointer for compatibility
    }
    else {
        return HTTPBadRequest;
    }

    return HTTPOK;
}

THTTPStatus CWebServer::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType)
{
    assert (pPath != 0);
    assert (ppContentType != 0);
    assert (m_pActLED != 0);

    THTTPStatus resultCode = HTTPOK;
    unsigned nLength = 0;

    // Get USB speed information
    boolean bUSBFullSpeed = CKernelOptions::Get()->GetUSBFullSpeed();
    const char* pUSBSpeed = bUSBFullSpeed ? "USB 1.1 (Full Speed)" : "USB 2.0 (High Speed)";

    LOGNOTE("Path: %s, Params: %s", pPath, pParams ? pParams : "");

    if ((strcmp (pPath, "/") == 0 || strcmp (pPath, "/index.html") == 0))
    {
        // Redirect to the list page instead of generating a homepage
        LOGNOTE("Redirecting to /list from %s", pPath);

        // Create a simple redirect page
        snprintf((char *)m_pContentBuffer, MAX_CONTENT_SIZE,
                 "<html><head><meta http-equiv=\"refresh\" content=\"0;URL='/list'\">"
                 "<title>Redirecting...</title></head>"
                 "<body>Redirecting to file list...</body></html>");

        nLength = strlen((char *)m_pContentBuffer);
        *ppContentType = "text/html; charset=utf-8";
    } 
    else if (strcmp (pPath, "/list") == 0) 
    { 
        // List images with HTML table formatting, passing parameters for pagination
        LOGNOTE("Calling list_files_as_table");
        resultCode = list_files_as_table((char*)m_pContentBuffer, MAX_CONTENT_SIZE, pParams, pUSBSpeed);
        nLength = strlen((char*)m_pContentBuffer);
        *ppContentType = "text/html; charset=utf-8";
    } else if (strcmp(pPath, "/api/list") == 0) {
        // List our images in JSON format (keep API endpoint for compatibility)
        resultCode = list_files_as_json((char *)m_pContentBuffer, MAX_CONTENT_SIZE);
        nLength = strlen((char *)m_pContentBuffer);
        *ppContentType = "application/json; charset=utf-8";
    } 
    else if (strcmp (pPath, "/system") == 0 && pParams && strncmp (pParams, "action=", 7) == 0) 
    { 
        // Handle system operation (shutdown/reboot)
        char actionValue[32];  // Buffer to hold either "shutdown" or "reboot"
        const char *equalSign = strchr(pParams, '=');
        if (equalSign && *(equalSign + 1) != '\0') {
            // Extract only the value until next & or end of string
            size_t i = 0;
            const char *p = equalSign + 1;
            while (*p && *p != '&' && i < sizeof(actionValue) - 1) {
                actionValue[i++] = *p++;
            }
            actionValue[i] = '\0';

            LOGNOTE("System action requested: %s", actionValue);
            
            resultCode = handle_system_operation((char*)m_pContentBuffer, MAX_CONTENT_SIZE, 
                                              actionValue, &m_ShutdownMode, pUSBSpeed);
            nLength = strlen((char*)m_pContentBuffer);
            *ppContentType = "text/html; charset=utf-8";
        } else {
            LOGERR("system action value is missing");
            strcpy((char *)m_pContentBuffer, "system action value is missing");
            nLength = strlen((char *)m_pContentBuffer);
            return HTTPBadRequest;
        }
    }
    else if (strcmp (pPath, "/mount") == 0 && pParams && strncmp (pParams, "file=", 5) == 0) 
    { 
        // Extract value (after '=')
        char pParamValue[MAX_FILENAME * 3];  // URL-encoded could be longer
        char decodedValue[MAX_FILENAME + 1];
        const char *equalSign = strchr(pParams, '=');
        if (equalSign && *(equalSign + 1) != '\0') {
            strncpy(pParamValue, equalSign + 1, sizeof(pParamValue) - 1);
            pParamValue[sizeof(pParamValue) - 1] = '\0';

            // URL decode the filename
            urldecode(decodedValue, pParamValue);
            LOGNOTE("Mounting file (decoded): %s", decodedValue);

            // Save current mounted image name
            m_pProperties->SetString("current_image", decodedValue);
            m_pProperties->Save();

            // Load the image
            CCueBinFileDevice *cueBinFileDevice = loadCueBinFileDevice(decodedValue);
            if (!cueBinFileDevice) {
                LOGERR("Failed to get cueBinFileDevice");
                return HTTPInternalServerError;
            }
            m_pCDGadget->SetDevice(cueBinFileDevice);

            // Generate a success page
            resultCode = generate_mount_success_page((char*)m_pContentBuffer, MAX_CONTENT_SIZE, decodedValue, pUSBSpeed);
            // Notify display about the image change
            NotifyDisplayUpdate(decodedValue);
            nLength = strlen((char*)m_pContentBuffer);
            *ppContentType = "text/html; charset=utf-8";
        } else {
            LOGERR("mount file value is missing");
            strcpy((char *)m_pContentBuffer, "mount file value is missing");
            nLength = 28;
            return HTTPBadRequest;
        }
    }
    // Update in the controller endpoint handler
    else if (strcmp (pPath, "/controller") == 0 && (strncmp (pParams, "mount=", 6) == 0)) 
    { 
        // Extract value (after '=')
        char pParamValue[MAX_FILENAME * 3];  // URL-encoded could be longer
        char decodedValue[MAX_FILENAME + 1];
        const char *equalSign = strchr(pParams, '=');
        if (equalSign && *(equalSign + 1) != '\0') {
            strncpy(pParamValue, equalSign + 1, sizeof(pParamValue) - 1);
            pParamValue[sizeof(pParamValue) - 1] = '\0';

            // URL decode the filename
            urldecode(decodedValue, pParamValue);
            LOGNOTE("Controller mounting file (decoded): %s", decodedValue);

            // Save current mounted image name
            m_pProperties->SetString("current_image", decodedValue);
            m_pProperties->Save();

            CCueBinFileDevice *cueBinFileDevice = loadCueBinFileDevice(decodedValue);
            if (!cueBinFileDevice) {
                LOGERR("Failed to get cueBinFileDevice");
                return HTTPInternalServerError;
            }

            m_pCDGadget->SetDevice(cueBinFileDevice);
            
            // Add explicit call to notify display update
            NotifyDisplayUpdate(decodedValue);
            
            strcpy((char*)m_pContentBuffer, "{\"status\": \"OK\"}");
            nLength = 16;
            *ppContentType = "application/json; charset=iso-8859-1";
        } else {
            LOGERR("mount value is missing");
            strcpy((char *)m_pContentBuffer, "mount value is missing");
            nLength = 22;
            return HTTPBadRequest;
        }
    }
    else
    {
        return HTTPNotFound;
    }

    assert (pLength != 0);
    if (*pLength < nLength)
    {
        LOGERR("Increase MAX_CONTENT_SIZE to at least %u", nLength);
        return HTTPInternalServerError;
    }

    assert(pBuffer != 0);
    assert(nLength > 0);
    memcpy(pBuffer, m_pContentBuffer, nLength);

    *pLength = nLength;

    LOGNOTE("Returning from GetContent %d", nLength);

    return resultCode;
}

TShutdownMode CWebServer::GetShutdownMode(void) const 
{
    return s_GlobalShutdownMode;
}

void CWebServer::SetGlobalShutdownMode(TShutdownMode mode) 
{
    s_GlobalShutdownMode = mode;
}

// Add a setter for the callback
void CWebServer::SetDisplayUpdateHandler(TDisplayUpdateHandler pHandler)
{
    LOGNOTE("Setting display update handler: %p", pHandler);
    m_pDisplayUpdateHandler = pHandler;
}
// Notifier method to call the callback
void CWebServer::NotifyDisplayUpdate(const char* imageName)
{
    if (m_pDisplayUpdateHandler != nullptr)
    {
        LOGNOTE("Calling display update handler for file: %s", imageName);
        (*m_pDisplayUpdateHandler)(imageName);
    }
    else
    {
        LOGERR("Display update handler is NULL - cannot update display");
    }
}
