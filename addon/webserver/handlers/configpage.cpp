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
#include <cdplayer/cdplayer.h>

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
    
    // Check if CD player is available (only in CDROM mode with sound enabled)
    CCDPlayer* pCDPlayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    bool soundTestAvailable = (pCDPlayer != nullptr);
    context["sound_test_available"] = soundTestAvailable;
    
    // Handle POST request (form submission)
    if (pFormData && strlen(pFormData) > 0) {
        LOGDBG("Processing configuration form data");
        
        auto form_params = ParseFormData(pFormData);
        
        // Handle sound test action
        if (form_params.count("action") && form_params["action"] == "soundtest") {
            LOGNOTE("Sound test button pressed");
            
            if (pCDPlayer) {
                if (pCDPlayer->SoundTest()) {
                    success_message = "Sound test executed successfully";
                } else {
                    error_message = "Sound test failed";
                }
            } else {
                error_message = "Error: CD Player not available (sound not enabled)";
            }
        } else {
            // Handle regular configuration updates
            
            // Display HAT configuration
            if (form_params.count("displayhat")) {
                config->SetDisplayHat(form_params["displayhat"].c_str());
            }
            
            // Screen timeout
            if (form_params.count("screen_timeout")) {
                config->SetScreenTimeout(std::atoi(form_params["screen_timeout"].c_str()));
            }
            
            // ST7789 brightness settings
            if (form_params.count("st7789_brightness")) {
                config->SetST7789Brightness(std::atoi(form_params["st7789_brightness"].c_str()));
            }
            
            if (form_params.count("st7789_sleep_brightness")) {
                config->SetST7789SleepBrightness(std::atoi(form_params["st7789_sleep_brightness"].c_str()));
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
    }
    
    // Set current values for display
    std::string current_displayhat = config->GetDisplayHat();
    std::string current_screen_timeout = std::to_string(config->GetScreenTimeout());
    std::string current_st7789_brightness = std::to_string(config->GetST7789Brightness());
    std::string current_st7789_sleep_brightness = std::to_string(config->GetST7789SleepBrightness());
    std::string current_default_volume = std::to_string(config->GetDefaultVolume());
    std::string current_sounddev = config->GetSoundDev();
    std::string current_loglevel = std::to_string(config->GetLogLevel());
    std::string current_usbspeed = config->GetUSBFullSpeed() ? "full" : "high";
    std::string current_logfile = config->GetLogfile();
    
    // Remove SD:/ prefix from logfile for display
    if (current_logfile.find("SD:/") == 0) {
        current_logfile = current_logfile.substr(4);
    }
    
    // Set context variables
    context["current_displayhat"] = current_displayhat;
    context["current_screen_timeout"] = current_screen_timeout;
    context["current_st7789_brightness"] = current_st7789_brightness;
    context["current_st7789_sleep_brightness"] = current_st7789_sleep_brightness;
    context["current_logfile"] = current_logfile.empty() ? "disabled" : current_logfile;
    context["current_default_volume"] = current_default_volume.empty() ? "255" : current_default_volume;
    context["current_sounddev"] = current_sounddev;
    context["current_loglevel"] = current_loglevel;
    context["current_usbspeed"] = current_usbspeed;

    // Set form values
    context["screen_timeout"] = current_screen_timeout;
    context["st7789_brightness"] = current_st7789_brightness;
    context["st7789_sleep_brightness"] = current_st7789_sleep_brightness;
    context["logfile"] = current_logfile;
    
    // Set display HAT options
    context["displayhat_none"] = (current_displayhat == "none");
    context["displayhat_pirateaudio"] = (current_displayhat == "pirateaudiolineout");
    context["displayhat_waveshare"] = (current_displayhat == "waveshare");
    context["displayhat_st7789"] = (current_displayhat == "st7789");
    context["displayhat_sh1106"] = (current_displayhat == "sh1106");
    
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
