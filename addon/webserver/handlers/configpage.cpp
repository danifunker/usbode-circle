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
#include "configpage.h"
#include "util.h"

using namespace kainjow;

LOGMODULE("configpagehandler");

char s_Config[] =
#include "config.h"
;

std::string ConfigPageHandler::GetHTML() {
    return std::string(s_Config);
}

std::map<std::string, std::string> ConfigPageHandler::ParseFormData(const char* pFormData) {
    std::map<std::string, std::string> params;
    
    if (!pFormData) {
        return params;
    }
    
    std::string formData(pFormData);
    std::istringstream iss(formData);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, pos));
            std::string value = url_decode(pair.substr(pos + 1));
            params[key] = value;
        }
    }
    
    return params;
}

std::map<std::string, std::string> ConfigPageHandler::ParseConfigFile() {
    std::map<std::string, std::string> config;
    
    FIL file;
    FRESULT result = f_open(&file, "SD:/config.txt", FA_READ);
    if (result != FR_OK) {
        LOGERR("Failed to open config.txt for reading: %d", result);
        return config;
    }
    
    char line[256];
    bool in_usbode_section = false;
    
    while (f_gets(line, sizeof(line), &file)) {
        std::string str_line(line);
        // Remove trailing newline/carriage return
        str_line.erase(str_line.find_last_not_of("\r\n") + 1);
        
        // Check for section headers
        if (str_line.find("[usbode]") != std::string::npos) {
            in_usbode_section = true;
            continue;
        } else if (str_line.find("[") != std::string::npos) {
            in_usbode_section = false;
            continue;
        }
        
        // Parse key=value pairs in usbode section
        if (in_usbode_section) {
            size_t pos = str_line.find('=');
            if (pos != std::string::npos) {
                std::string key = str_line.substr(0, pos);
                std::string value = str_line.substr(pos + 1);
                config[key] = value;
            }
        }
    }
    
    f_close(&file);
    return config;
}

std::map<std::string, std::string> ConfigPageHandler::ParseCmdlineFile() {
    std::map<std::string, std::string> cmdline;
    
    FIL file;
    FRESULT result = f_open(&file, "SD:/cmdline.txt", FA_READ);
    if (result != FR_OK) {
        LOGERR("Failed to open cmdline.txt for reading: %d", result);
        return cmdline;
    }
    
    char line[512];
    if (f_gets(line, sizeof(line), &file)) {
        std::string str_line(line);
        // Remove trailing newline/carriage return
        str_line.erase(str_line.find_last_not_of("\r\n") + 1);
        
        std::istringstream iss(str_line);
        std::string param;
        
        while (iss >> param) {
            size_t pos = param.find('=');
            if (pos != std::string::npos) {
                std::string key = param.substr(0, pos);
                std::string value = param.substr(pos + 1);
                cmdline[key] = value;
            } else {
                cmdline[param] = "true";
            }
        }
    }
    
    f_close(&file);
    return cmdline;
}

bool ConfigPageHandler::UpdateConfigFile(const std::map<std::string, std::string>& config_params, std::string& error_message) {
    // First, read the entire config file
    FIL read_file;
    FRESULT result = f_open(&read_file, "SD:/config.txt", FA_READ);
    if (result != FR_OK) {
        error_message = "Failed to open config.txt for reading";
        return false;
    }
    
    std::vector<std::string> lines;
    char line[256];
    bool in_usbode_section = false;
    std::map<std::string, bool> params_written;
    
    // Initialize all params as not written
    for (const auto& param : config_params) {
        params_written[param.first] = false;
    }
    
    while (f_gets(line, sizeof(line), &read_file)) {
        std::string str_line(line);
        // Remove trailing newline/carriage return
        str_line.erase(str_line.find_last_not_of("\r\n") + 1);
        
        // Check for section headers
        if (str_line.find("[usbode]") != std::string::npos) {
            in_usbode_section = true;
            lines.push_back(str_line);
            continue;
        } else if (str_line.find("[") != std::string::npos) {
            // Before leaving usbode section, add any unwritten params
            if (in_usbode_section) {
                for (const auto& param : config_params) {
                    if (!params_written[param.first] && !param.second.empty()) {
                        lines.push_back(param.first + "=" + param.second);
                        params_written[param.first] = true;
                    }
                }
            }
            in_usbode_section = false;
            lines.push_back(str_line);
            continue;
        }
        
        // Handle lines in usbode section
        if (in_usbode_section) {
            size_t pos = str_line.find('=');
            if (pos != std::string::npos) {
                std::string key = str_line.substr(0, pos);
                
                // Check if this is a parameter we want to update
                auto it = config_params.find(key);
                if (it != config_params.end()) {
                    if (!it->second.empty()) {
                        lines.push_back(key + "=" + it->second);
                    }
                    // If empty, don't add the line (effectively removing it)
                    params_written[key] = true;
                } else {
                    // Keep existing line if not in our update list
                    lines.push_back(str_line);
                }
            } else {
                lines.push_back(str_line);
            }
        } else {
            lines.push_back(str_line);
        }
    }
    
    // If we're still in usbode section at end of file, add unwritten params
    if (in_usbode_section) {
        for (const auto& param : config_params) {
            if (!params_written[param.first] && !param.second.empty()) {
                lines.push_back(param.first + "=" + param.second);
            }
        }
    }
    
    f_close(&read_file);
    
    // Now write the updated content back
    FIL write_file;
    result = f_open(&write_file, "SD:/config.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) {
        error_message = "Failed to open config.txt for writing";
        return false;
    }
    
    for (const auto& line : lines) {
        UINT bytes_written;
        std::string line_with_newline = line + "\n";
        result = f_write(&write_file, line_with_newline.c_str(), line_with_newline.length(), &bytes_written);
        if (result != FR_OK) {
            f_close(&write_file);
            error_message = "Failed to write to config.txt";
            return false;
        }
    }
    
    f_close(&write_file);
    return true;
}

bool ConfigPageHandler::UpdateCmdlineFile(const std::map<std::string, std::string>& cmdline_params, std::string& error_message) {
    // Read current cmdline.txt
    auto current_params = ParseCmdlineFile();
    
    // Update with new parameters
    for (const auto& param : cmdline_params) {
        if (param.second.empty()) {
            // Remove parameter if value is empty
            current_params.erase(param.first);
        } else {
            current_params[param.first] = param.second;
        }
    }
    
    // Build new cmdline string
    std::string cmdline_content;
    for (const auto& param : current_params) {
        if (!cmdline_content.empty()) {
            cmdline_content += " ";
        }
        
        if (param.second == "true") {
            cmdline_content += param.first;
        } else {
            cmdline_content += param.first + "=" + param.second;
        }
    }
    
    // Write back to file
    FIL file;
    FRESULT result = f_open(&file, "SD:/cmdline.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) {
        error_message = "Failed to open cmdline.txt for writing";
        return false;
    }
    
    UINT bytes_written;
    cmdline_content += "\n";
    result = f_write(&file, cmdline_content.c_str(), cmdline_content.length(), &bytes_written);
    if (result != FR_OK) {
        f_close(&file);
        error_message = "Failed to write to cmdline.txt";
        return false;
    }
    
    f_close(&file);
    return true;
}

THTTPStatus ConfigPageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   CPropertiesFatFsFile *m_pProperties,
                                   CUSBCDGadget *pCDGadget)
{
    LOGNOTE("Config page called");
    
    std::string error_message;
    std::string success_message;
    
    // Handle POST request (form submission)
    if (pFormData && strlen(pFormData) > 0) {
        LOGNOTE("Processing configuration form data");
        
        auto form_params = ParseFormData(pFormData);
        
        // Prepare config.txt updates
        std::map<std::string, std::string> config_updates;
        
        // Display HAT configuration
        if (form_params.count("displayhat")) {
            config_updates["displayhat"] = form_params["displayhat"];
        }
        
        // Screen timeout
        if (form_params.count("screen_timeout")) {
            config_updates["screen_timeout"] = form_params["screen_timeout"];
        }
        
        // Log file configuration
        if (form_params.count("logfile")) {
            std::string logfile = form_params["logfile"];
            if (!logfile.empty()) {
                // Ensure SD:/ prefix
                if (logfile.find("SD:/") != 0) {
                    logfile = "SD:/" + logfile;
                }
                config_updates["logfile"] = logfile;
            } else {
                config_updates["logfile"] = ""; // Will remove the line
            }
        }
        
        // Prepare cmdline.txt updates
        std::map<std::string, std::string> cmdline_updates;
        
        // Sound device configuration
        if (form_params.count("sounddev")) {
            cmdline_updates["sounddev"] = form_params["sounddev"];
        }
        
        // Log level configuration
        if (form_params.count("loglevel")) {
            cmdline_updates["loglevel"] = form_params["loglevel"];
        }
        
        // USB speed configuration
        if (form_params.count("usbspeed")) {
            if (form_params["usbspeed"] == "full") {
                cmdline_updates["usbspeed"] = "full";
            } else {
                cmdline_updates["usbspeed"] = ""; // Will remove the parameter
            }
        }
        
        // Update files
        bool config_success = true;
        bool cmdline_success = true;
        
        if (!config_updates.empty()) {
            config_success = UpdateConfigFile(config_updates, error_message);
        }
        
        if (!cmdline_updates.empty() && config_success) {
            cmdline_success = UpdateCmdlineFile(cmdline_updates, error_message);
        }
        
        if (config_success && cmdline_success) {
            // Check for action parameter to determine what to do after saving
            std::string action = form_params.count("action") ? form_params["action"] : "save";
            
            if (action == "save_reboot") {
                success_message = "Configuration saved successfully. Rebooting in 3 seconds...";
                // Schedule a reboot in 3 seconds
                CShutdown *pShutdown = new CShutdown(ShutdownReboot, 3000);
                pShutdown->Start();
            } else if (action == "save_shutdown") {
                success_message = "Configuration saved successfully. Shutting down in 3 seconds...";
                // Schedule a shutdown in 3 seconds
                CShutdown *pShutdown = new CShutdown(ShutdownHalt, 3000);
                pShutdown->Start();
            } else {
                success_message = "Configuration saved successfully. Reboot required for changes to take effect.";
            }
        }
    }
    
    // Read current configuration
    auto config_data = ParseConfigFile();
    auto cmdline_data = ParseCmdlineFile();
    
    // Set current values for display
    std::string current_displayhat = config_data.count("displayhat") ? config_data["displayhat"] : "none";
    std::string current_screen_timeout = config_data.count("screen_timeout") ? config_data["screen_timeout"] : "5";
    std::string current_logfile = config_data.count("logfile") ? config_data["logfile"] : "";
    std::string current_sounddev = cmdline_data.count("sounddev") ? cmdline_data["sounddev"] : "sndpwm";
    std::string current_loglevel = cmdline_data.count("loglevel") ? cmdline_data["loglevel"] : "4";
    std::string current_usbspeed = cmdline_data.count("usbspeed") ? "full" : "high";
    
    // Remove SD:/ prefix from logfile for display
    if (current_logfile.find("SD:/") == 0) {
        current_logfile = current_logfile.substr(4);
    }
    
    // Set context variables
    context["current_displayhat"] = current_displayhat;
    context["current_screen_timeout"] = current_screen_timeout;
    context["current_logfile"] = current_logfile.empty() ? "disabled" : current_logfile;
    context["current_sounddev"] = current_sounddev;
    context["current_loglevel"] = current_loglevel;
    context["current_usbspeed"] = current_usbspeed;
    
    // Set form values
    context["screen_timeout"] = current_screen_timeout;
    context["logfile"] = current_logfile;
    
    // Set display HAT options
    context["displayhat_none"] = (current_displayhat == "none");
    context["displayhat_pirateaudio"] = (current_displayhat == "pirateaudiolineout");
    context["displayhat_waveshare"] = (current_displayhat == "waveshare");
    
    // Set sound device options
    context["sounddev_sndpwm"] = (current_sounddev == "sndpwm");
    context["sounddev_sndi2s"] = (current_sounddev == "sndi2s");
    
    // Set USB speed options
    context["usbspeed_high"] = (current_usbspeed == "high");
    context["usbspeed_full"] = (current_usbspeed == "full");
    
    // Set log level options
    context["loglevel_0"] = (current_loglevel == "0");
    context["loglevel_1"] = (current_loglevel == "1");
    context["loglevel_2"] = (current_loglevel == "2");
    context["loglevel_3"] = (current_loglevel == "3");
    context["loglevel_4"] = (current_loglevel == "4");
    context["loglevel_5"] = (current_loglevel == "5");
    
    // Set messages
    if (!error_message.empty()) {
        context["error_message"] = error_message;
    }
    if (!success_message.empty()) {
        context["success_message"] = success_message;
    }
    
    return HTTPOK;
}
