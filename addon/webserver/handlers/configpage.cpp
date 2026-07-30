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
#include "../util.h"
#include <configservice/configservice.h>
#include <cdplayer/cdplayer.h>
#include <filelogdaemon/filelogdaemon.h>
#include "../webglobals.h"


using namespace kainjow;

LOGMODULE("configpagehandler");

char s_Config[] =
#include "config.h"
;

std::string ConfigPageHandler::GetHTML() {
    return std::string(s_Config);
}

// Normalize onto volume 0: and reject what FatFs cannot open; a bad path is not
// otherwise discovered until the next boot. Empty means logging is off.
static bool NormalizeLogfilePath(std::string& path, std::string& error) {
    const size_t first = path.find_first_not_of(" \t");
    if (first == std::string::npos) {
        path.clear();
        return true;
    }
    path = path.substr(first, path.find_last_not_of(" \t") - first + 1);

    // FatFs accepts either separator and users type both.
    std::replace(path.begin(), path.end(), '\\', '/');

    // Refuse any volume but the boot partition: 1: is real but is not mounted
    // until well after the log daemon starts.
    const size_t colon = path.find(':');
    if (colon != std::string::npos) {
        const std::string volume = path.substr(0, colon);
        if (volume != "0") {
            error = "Log file must be on the boot partition. Remove the \"" + volume +
                    ":\" prefix and give a path like usbode-log.txt.";
            return false;
        }
        path.erase(0, colon + 1);
    }
    while (!path.empty() && path[0] == '/') {
        path.erase(0, 1);
    }

    if (path.empty() || path.back() == '/') {
        error = "Log file path has to name a file, not a directory.";
        return false;
    }
    if (path.find(':') != std::string::npos) {
        error = "Log file path cannot contain a colon.";
        return false;
    }

    // FatFs will not create a directory. f_opendir, not f_stat, which cannot
    // describe a volume root.
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string dir = path.substr(0, slash);
        DIR probe;
        if (f_opendir(&probe, ("0:/" + dir).c_str()) != FR_OK) {
            error = "Directory \"" + dir + "\" does not exist on the boot partition.";
            return false;
        }
        f_closedir(&probe);
    }

    path = "0:/" + path;
    return true;
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
                    context["message"] = "Sound test executed successfully";
                } else {
                    context["message"] = "Sound test failed";
                }
            } else {
                context["message"] = "Error: CD Player not available (sound not enabled)";
            }
        } else {
            // Handle regular configuration updates
            
            // Display HAT configuration
            if (form_params.count("displayhat")) {
                config->SetDisplayHat(form_params["displayhat"].c_str());
            }

            // Theme configuration
            if (form_params.count("theme")) {
                config->SetTheme(form_params["theme"].c_str());
            }
            
            // Low power timeout
            if (form_params.count("low_power_timeout")) {
                config->SetLowPowerTimeout(std::atoi(form_params["low_power_timeout"].c_str()));
            }

            // Screen timeout (sleep timeout)
            if (form_params.count("screen_timeout")) {
                config->SetScreenTimeout(std::atoi(form_params["screen_timeout"].c_str()));
            }

            // ST7789 brightness settings
            if (form_params.count("st7789_brightness")) {
                config->SetST7789Brightness(std::atoi(form_params["st7789_brightness"].c_str()));
            }

            if (form_params.count("st7789_low_power_brightness")) {
                config->SetST7789LowPowerBrightness(std::atoi(form_params["st7789_low_power_brightness"].c_str()));
            }

            if (form_params.count("st7789_sleep_brightness")) {
                config->SetST7789SleepBrightness(std::atoi(form_params["st7789_sleep_brightness"].c_str()));
            }
            
            // An empty value means "off"; the write path used to ignore it.
            if (form_params.count("logfile")) {
                std::string logfile = form_params["logfile"];
                std::string logfileError;
                if (NormalizeLogfilePath(logfile, logfileError)) {
                    config->SetLogfile(logfile.c_str());
                } else {
                    error_message = logfileError;
                    LOGWARN("Rejected log file path '%s': %s",
                            form_params["logfile"].c_str(), logfileError.c_str());
                }
            }
            
            // Default volume configuration  
            if (form_params.count("default_volume")) { 
                config->SetDefaultVolume(std::atoi(form_params["default_volume"].c_str()));
            }
            
            // Sound device configuration
            if (form_params.count("sounddev")) {
                config->SetSoundDev(form_params["sounddev"].c_str());
            }
            
            // Log level configuration
            if (form_params.count("loglevel")) {
                int loglevel = std::atoi(form_params["loglevel"].c_str());
                if (loglevel < 0) loglevel = 0;
                if (loglevel > 5) loglevel = 5;
                config->SetLogLevel(loglevel);
                // Apply to the file log immediately; no reboot needed
                if (CFileLogDaemon::Get() != nullptr) {
                    CFileLogDaemon::Get()->SetLogLevel(loglevel);
                }
            }

            // Trace Lab configuration
            if (form_params.count("trace_mode")) {
                config->SetProperty("trace_mode", form_params["trace_mode"].c_str());
            }

            if (form_params.count("trace_buffer_kb")) {
                int kb = std::atoi(form_params["trace_buffer_kb"].c_str());
                if (kb > 0) {
                    config->SetProperty("trace_buffer_kb", (unsigned)kb);
                }
            }
            
            // USB speed configuration
            if (form_params.count("usbspeed")) {
                if (form_params["usbspeed"] == "full") {
                    config->SetUSBFullSpeed(true);
                } else {
                    config->SetUSBFullSpeed(false);
                }
            }
            
            // USB Target OS configuration
            if (form_params.count("usbtargetos")) {
                config->SetUSBTargetOS(ConfigService::StringToUSBTargetOS(form_params["usbtargetos"].c_str()));
            }

            // Folder navigation configuration
            if (form_params.count("folder_navigation")) {
                config->SetFlatFileList(form_params["folder_navigation"] == "off");
            }
            
            // Check for action parameter to determine what to do after saving
            std::string action = form_params.count("action") ? form_params["action"] : "save";

            // "Saved successfully" next to a rejection would read as though the
            // rejected value went in too.
            if (!error_message.empty()) {
                error_message += " Other settings were saved.";
                // The reboot used to be dropped silently here, leaving the user
                // waiting for one that was never coming.
                if (action == "save_reboot") {
                    error_message += " The reboot was cancelled so you can correct this.";
                } else if (action == "save_shutdown") {
                    error_message += " The shutdown was cancelled so you can correct this.";
                }
            } else if (action == "save_reboot") {
                success_message = "Configuration saved successfully. Rebooting in 3 seconds...";
                // Schedule a reboot in 3 seconds
                new CShutdown(ShutdownReboot, 3000);
            } else if (action == "save_shutdown") {
                success_message = "Configuration saved successfully. Shutting down in 3 seconds...";
                // Schedule a shutdown in 3 seconds
                new CShutdown(ShutdownHalt, 3000);
            } else {
                success_message = "Configuration saved successfully. Log level applies immediately; other changes take effect after a reboot.";
            }
        }
    }
    
    // The boot-time warning goes to the serial console, which SCREEN_HEADLESS
    // has no equivalent of.
    {
        char status[256] = {0};
        if (CFileLogDaemon::Get() != nullptr) {
            CFileLogDaemon::Get()->GetStatusText(status, sizeof(status));
        }
        context["logfile_status"] = std::string(status);
        context["logfile_broken"] = (CFileLogDaemon::Get() != nullptr &&
                                     !CFileLogDaemon::Get()->IsFileLogging() &&
                                     CFileLogDaemon::Get()->GetLogFilePath()[0] != '\0');
    }

    // Set current values for display
    std::string current_displayhat = config->GetDisplayHat();
    std::string current_low_power_timeout = std::to_string(config->GetLowPowerTimeout());
    std::string current_screen_timeout = std::to_string(config->GetScreenTimeout());
    std::string current_st7789_brightness = std::to_string(config->GetST7789Brightness());
    std::string current_st7789_low_power_brightness = std::to_string(config->GetST7789LowPowerBrightness());
    std::string current_st7789_sleep_brightness = std::to_string(config->GetST7789SleepBrightness());
    std::string current_default_volume = std::to_string(config->GetDefaultVolume());
    std::string current_sounddev = config->GetSoundDev();
    std::string current_loglevel = std::to_string(config->GetLogLevel());
    std::string current_trace_mode = config->GetProperty("trace_mode", "off");
    std::string current_trace_buffer_kb = std::to_string(config->GetProperty("trace_buffer_kb", 128U));
    std::string current_usbspeed = config->GetUSBFullSpeed() ? "full" : "high";
    std::string current_logfile = config->GetLogfile();
    std::string current_theme = config->GetTheme();
    std::string current_usbtargetos = ConfigService::USBTargetOSToString(config->GetUSBTargetOS());
    bool current_flat_file_list = config->GetFlatFileList();

    // Remove 0:/ prefix from logfile for display
    if (current_logfile.find("0:/") == 0) {
        current_logfile = current_logfile.substr(3);
    }

    // Create options html for available themes
    std::string themeOptionsHtml;
    const auto& themes = CWebGlobals::Get()->GetThemes();
    for (const auto& themeName : themes)
    {
        themeOptionsHtml += "\n<option value=\"";
        themeOptionsHtml += themeName;
        themeOptionsHtml += "\"";
        if (themeName == current_theme)
        {
            themeOptionsHtml += " selected";
        }
        themeOptionsHtml += ">";
        themeOptionsHtml += themeName;
        themeOptionsHtml += "</option>";
    }
    context["theme_options"] = themeOptionsHtml.c_str();
    
    // Set context variables
    context["current_displayhat"] = current_displayhat;
    context["current_low_power_timeout"] = current_low_power_timeout;
    context["current_screen_timeout"] = current_screen_timeout;
    context["current_st7789_brightness"] = current_st7789_brightness;
    context["current_st7789_low_power_brightness"] = current_st7789_low_power_brightness;
    context["current_st7789_sleep_brightness"] = current_st7789_sleep_brightness;
    context["current_logfile"] = current_logfile.empty() ? "disabled" : current_logfile;
    context["current_default_volume"] = current_default_volume.empty() ? "255" : current_default_volume;
    context["current_sounddev"] = current_sounddev;
    context["current_loglevel"] = current_loglevel;
    context["current_usbspeed"] = current_usbspeed;
    context["current_theme"] = current_theme;

    // Set form values
    context["low_power_timeout"] = current_low_power_timeout;
    context["screen_timeout"] = current_screen_timeout;
    context["st7789_brightness"] = current_st7789_brightness;
    context["st7789_low_power_brightness"] = current_st7789_low_power_brightness;
    context["st7789_sleep_brightness"] = current_st7789_sleep_brightness;
    context["logfile"] = current_logfile;
    
    // Set display HAT options
    context["displayhat_none"] = (current_displayhat == "none");
    context["displayhat_pirateaudio"] = (current_displayhat == "pirateaudiolineout");
    context["displayhat_pirateaudio_matte"] = (current_displayhat == "pirateaudiolineout-matte");
    context["displayhat_waveshare"] = (current_displayhat == "waveshare");
    context["displayhat_st7789"] = (current_displayhat == "st7789");
    context["displayhat_sh1106"] = (current_displayhat == "sh1106");
    context["displayhat_mt32pi"] = (current_displayhat == "mt32pi");

    // Set sound device options
    context["sounddev_sndpwm"] = (current_sounddev == "sndpwm");
    context["sounddev_sndi2s"] = (current_sounddev == "sndi2s");
    context["sounddev_sndhdmi"] = (current_sounddev == "sndhdmi");
    context["sounddev_none"] = (current_sounddev == "none");

    // Set USB speed options
    context["usbspeed_high"] = (current_usbspeed == "high");
    context["usbspeed_full"] = (current_usbspeed == "full");
    
    // Set USB Target OS options
    context["current_usbtargetos"] = current_usbtargetos;
    context["usbtargetos_doswin"] = (current_usbtargetos == "doswin");
    context["usbtargetos_apple"] = (current_usbtargetos == "apple");

    // Set folder navigation options
    context["folder_navigation_on"] = !current_flat_file_list;
    context["folder_navigation_off"] = current_flat_file_list;
    
    // Set log level options
    context["loglevel_0"] = (current_loglevel == "0");
    context["loglevel_1"] = (current_loglevel == "1");
    context["loglevel_2"] = (current_loglevel == "2");
    context["loglevel_3"] = (current_loglevel == "3");
    context["loglevel_4"] = (current_loglevel == "4");
    context["loglevel_5"] = (current_loglevel == "5");

    // Set trace mode options
    context["current_trace_mode"] = current_trace_mode;
    context["trace_mode_off"] = (current_trace_mode == "off");
    context["trace_mode_standard"] = (current_trace_mode == "standard");
    context["trace_mode_deep"] = (current_trace_mode == "deep");
    context["current_trace_buffer_kb"] = current_trace_buffer_kb;
    context["trace_buffer_kb"] = current_trace_buffer_kb;
    
    // Set messages
    if (!error_message.empty()) {
        context["error_message"] = error_message;
    }
    if (!success_message.empty()) {
        context["success_message"] = success_message;
    }
    
    return HTTPOK;
}
