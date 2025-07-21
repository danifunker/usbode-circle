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
#include <configservice/configservice.h>

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

THTTPStatus ConfigPageHandler::PopulateContext(kainjow::mustache::data& context,
                                   const char *pPath,
                                   const char  *pParams,
                                   const char  *pFormData)
{
    LOGNOTE("Config page called");
    
    std::string error_message;
    std::string success_message;
    ConfigService* config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
    
    // Handle POST request (form submission)
    if (pFormData && strlen(pFormData) > 0) {
        LOGDBG("Processing configuration form data");
        
        auto form_params = ParseFormData(pFormData);
        
        // Prepare config.txt updates
        std::map<std::string, std::string> config_updates;
        
        // Display HAT configuration
        if (form_params.count("displayhat")) {
	    config->SetDisplayHat(form_params["displayhat"].c_str());
        }
        
        // Screen timeout
        if (form_params.count("screen_timeout")) {
	    config->SetScreenTimeout(std::atoi(form_params["screen_timeout"].c_str()));
        }
        
        // Log file configuration
        if (form_params.count("logfile")) {
            std::string logfile = form_params["logfile"];
            if (!logfile.empty()) {
                // Ensure SD:/ prefix
                if (logfile.find("SD:/") != 0) {
                    logfile = "SD:/" + logfile;
                }
		config->SetLogfile(logfile.c_str());
            }
            if (form_params.count("default_volume"))
            { 
		config->SetDefaultVolume(std::atoi(form_params["default_volume"].c_str()));
            }
        }
        
        // Prepare cmdline.txt updates
        std::map<std::string, std::string> cmdline_updates;
        
        // Sound device configuration
        if (form_params.count("sounddev")) {
	    config->SetSoundDev(form_params["sounddev"].c_str());
        }
        
        // Log level configuration
        if (form_params.count("loglevel")) {
	    config->SetLogLevel(std::atoi(form_params["loglevel"].c_str()));
        }
        
        // USB speed configuration
        if (form_params.count("usbspeed")) {
            if (form_params["usbspeed"] == "full") {
		config->SetUSBFullSpeed(true);
            } else {
		config->SetUSBFullSpeed(false);
            }
        }
        
            // Check for action parameter to determine what to do after saving
            std::string action = form_params.count("action") ? form_params["action"] : "save";
            
            if (action == "save_reboot") {
                success_message = "Configuration saved successfully. Rebooting in 3 seconds...";
                // Schedule a reboot in 3 seconds
                new CShutdown(ShutdownReboot, 3000);
            } else if (action == "save_shutdown") {
                success_message = "Configuration saved successfully. Shutting down in 3 seconds...";
                // Schedule a shutdown in 3 seconds
                new CShutdown(ShutdownHalt, 3000);
            } else {
                success_message = "Configuration saved successfully. Reboot required for changes to take effect.";
            }
    }
    
    LOGNOTE("Loading current values");
    // Set current values for display
    std::string current_displayhat = config->GetDisplayHat();
    LOGNOTE("Loading current values");
    std::string current_screen_timeout = std::to_string(config->GetScreenTimeout());
    LOGNOTE("Loading current values");
    std::string current_default_volume = std::to_string(config->GetDefaultVolume());
    LOGNOTE("Loading current values");
    std::string current_sounddev = config->GetSoundDev();
    LOGNOTE("Loading current values");
    std::string current_loglevel = std::to_string(config->GetLogLevel());
    LOGNOTE("Loading current values");
    std::string current_usbspeed = config->GetUSBFullSpeed() ? "full" : "high";
    LOGNOTE("Loading current values");
    std::string current_logfile = config->GetLogfile();

    LOGNOTE("Loaded current values");
    
    // Remove SD:/ prefix from logfile for display
    if (current_logfile.find("SD:/") == 0) {
        current_logfile = current_logfile.substr(4);
    }
    
    // Set context variables
    context["current_displayhat"] = current_displayhat;
    context["current_screen_timeout"] = current_screen_timeout;
    context["current_logfile"] = current_logfile.empty() ? "disabled" : current_logfile;
    context["current_default_volume"] = current_default_volume.empty() ? "255" : current_default_volume;
    context["current_sounddev"] = current_sounddev;
    context["current_loglevel"] = current_loglevel;
    context["current_usbspeed"] = current_usbspeed;
    LOGNOTE("Set context 1");

    // Set form values
    context["screen_timeout"] = current_screen_timeout;
    context["logfile"] = current_logfile;
    LOGNOTE("Set context 2");
    
    // Set display HAT options
    context["displayhat_none"] = (current_displayhat == "none");
    context["displayhat_pirateaudio"] = (current_displayhat == "pirateaudiolineout");
    context["displayhat_waveshare"] = (current_displayhat == "waveshare");
    context["displayhat_st7789"] = (current_displayhat == "st7789");
    LOGNOTE("Set context 3");
    
    // Set sound device options
    context["sounddev_sndpwm"] = (current_sounddev == "sndpwm");
    context["sounddev_sndi2s"] = (current_sounddev == "sndi2s");
    
    // Set USB speed options
    context["usbspeed_high"] = (current_usbspeed == "high");
    context["usbspeed_full"] = (current_usbspeed == "full");
    LOGNOTE("Set context 4");
    
    // Set log level options
    context["loglevel_0"] = (current_loglevel == "0");
    context["loglevel_1"] = (current_loglevel == "1");
    context["loglevel_2"] = (current_loglevel == "2");
    context["loglevel_3"] = (current_loglevel == "3");
    context["loglevel_4"] = (current_loglevel == "4");
    context["loglevel_5"] = (current_loglevel == "5");
    LOGNOTE("Set context 5");
    
    // Set messages
    if (!error_message.empty()) {
        context["error_message"] = error_message;
    }
    if (!success_message.empty()) {
        context["success_message"] = success_message;
    }
    LOGNOTE("Set context 6");
    
    return HTTPOK;
}
