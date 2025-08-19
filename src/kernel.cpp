//
// kernel.cpp
//
// Test for USB Mass Storage Gadget by Mike Messinides
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
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
#include "kernel.h"

#include <ftpserver/ftpdaemon.h>
#include <string.h>

#include <gitinfo/gitinfo.h>
#include <discimage/util.h>
#include <webserver/webserver.h>
#include <devicestate/devicestate.h>
#include <circle/logger.h>
#include <displayservice/displayservice.h>
#include <configservice/configservice.h>
#include <setupstatus/setupstatus.h>

#include <circle/time.h>

#define DRIVE "0:"
#define FIRMWARE_PATH DRIVE "/firmware/"
#define SUPPLICANT_CONFIG_FILE DRIVE "/wpa_supplicant.conf"
#define CONFIG_FILE DRIVE "/config.txt"
#define LOG_FILE DRIVE "/logfile.txt"
#define HOSTNAME "usbode"
#define SPI_MASTER_DEVICE 0

// Define the images directory
#define IMAGES_DIR "0:/images"

LOGMODULE("kernel");

static CKernel* g_pKernel = nullptr;

CKernel::CKernel(void)
    : m_Screen(m_Options.GetWidth(), m_Options.GetHeight()),
      m_Timer(&m_Interrupt),
      m_Logger(m_Options.GetLogLevel(), &m_Timer),
      m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
      m_WLAN(FIRMWARE_PATH),
      m_Net(0, 0, 0, 0, HOSTNAME, NetDeviceTypeWLAN),
      m_WPASupplicant(SUPPLICANT_CONFIG_FILE),
      m_pSPIMaster(nullptr)
{
    // Initialize the global kernel pointer 
    g_pKernel = this;
}

CKernel::~CKernel(void) {
    if (m_pSPIMaster != nullptr) {
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
    }
    //TODO delete more here!
}

boolean CKernel::Initialize(void) {
    boolean bOK = TRUE;

    if (bOK) {
        bOK = m_Screen.Initialize();
        LOGNOTE("Initialized screen");
    }

    if (bOK) {
        bOK = m_Serial.Initialize(115200);
        LOGNOTE("Initialized serial");
    }

    if (bOK) {
        CDevice* pTarget = m_DeviceNameService.GetDevice(m_Options.GetLogDevice(), FALSE);
        if (pTarget == 0) {
            pTarget = &m_Screen;
        }

        bOK = m_Logger.Initialize(pTarget);
        LOGNOTE("Initialized logger");
    }

    if (bOK) {
        bOK = m_Interrupt.Initialize();
        LOGNOTE("Initialized interrupts");
    }

    if (bOK) {
        bOK = m_Timer.Initialize();
        LOGNOTE("Initialized timer");
    }

    if (bOK) {
        bOK = m_EMMC.Initialize();
        LOGNOTE("Initialized eMMC");
    }

    if (bOK) {
        if (f_mount(&m_FileSystem, DRIVE, 1) != FR_OK) {
            LOGERR("Cannot mount drive: %s", DRIVE);

            bOK = FALSE;
        }
        LOGNOTE("Initialized filesystem");
    }

    if (bOK) {
        bOK = m_WLAN.Initialize();
        LOGNOTE("Initialized WLAN");
    }

    if (bOK) {
        bOK = m_Net.Initialize(FALSE);
        LOGNOTE("Initialized network");
    }

    if (bOK) {
        bOK = m_WPASupplicant.Initialize();
        LOGNOTE("Initialized WAP supplicant");
    }

    return bOK;
}

CKernel* CKernel::Get() {
    return g_pKernel;
}

TShutdownMode CKernel::Run(void) {
	

    // Initialize our config service
    ConfigService* config = new ConfigService();
    LOGNOTE("Started Config service");

    // Start the SetupStatus service early
    new SetupStatus();
    LOGNOTE("Started SetupStatus service");

    // Start the file logging service
    const char* logfile = config->GetLogfile();
    if (logfile) {
        new CFileLogDaemon(logfile);
        LOGNOTE("Started the Log File service");
    }

    // Announce ourselves
    LOGNOTE("=====================================");
    LOGNOTE("Welcome to USBODE");
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);
    LOGNOTE("Git Info: %s @ %s", GIT_BRANCH, GIT_COMMIT);
    LOGNOTE("=====================================");

    int mode = config->GetMode();
    LOGNOTE("Got mode = %d", mode);

    if (mode == 0) { // CDROM Mode


	    // Initialize the CD Player service
	    const char* pSoundDevice = m_Options.GetSoundDevice();
	    
	    //Currently supporting PWM and I2S sound devices. HDMI needs more work.
	    if (strcmp(pSoundDevice, "sndi2s") == 0 || strcmp(pSoundDevice, "sndpwm") == 0) {
    		unsigned int volume = config->GetDefaultVolume();
		if (volume > 0xff)
			volume = 0xff;
		CCDPlayer* player = new CCDPlayer(pSoundDevice);
		player->SetDefaultVolume((u8)volume);
		LOGNOTE("Started the CD Player service. Default volume is %d", volume);
	    }

	    // Add SD Card partition logging here (comprehensive scan)
	    LOGNOTE("Analyzing SD Card partitions...");
	    
	    // Check multiple partitions (0: through 3:)
	    for (int partition = 0; partition <= 3; partition++) {
	        CString drive;
	        drive.Format("%d:", partition);
	        
	        LOGNOTE("Checking partition %d (%s):", partition, (const char*)drive);
	        
	        DWORD free_clusters;
	        FATFS* fs;
	        FRESULT res = f_getfree((const char*)drive, &free_clusters, &fs);
	        
	        if (res == FR_OK && fs != nullptr) {
	            // Calculate sizes using 64-bit arithmetic to avoid overflow
	            DWORD total_clusters = fs->n_fatent - 2;
	            DWORD cluster_size = fs->csize; // sectors per cluster
	            
	            // Use 64-bit arithmetic to prevent overflow
	            uint64_t total_bytes = (uint64_t)total_clusters * cluster_size * 512;
	            uint64_t free_bytes = (uint64_t)free_clusters * cluster_size * 512;
	            
	            // Convert to MB and GB for display
	            uint32_t total_mb = (uint32_t)(total_bytes / (1024 * 1024));
	            uint32_t free_mb = (uint32_t)(free_bytes / (1024 * 1024));
	            uint32_t total_gb = (uint32_t)(total_bytes / (1024 * 1024 * 1024));
	            uint32_t free_gb = (uint32_t)(free_bytes / (1024 * 1024 * 1024));
	            
	            // Get filesystem type
	            const char* fs_type = "Unknown";
	            switch (fs->fs_type) {
	                case FS_FAT12: fs_type = "FAT12"; break;
	                case FS_FAT16: fs_type = "FAT16"; break;
	                case FS_FAT32: fs_type = "FAT32"; break;
	                case FS_EXFAT: fs_type = "exFAT"; break;
	            }
	            
	            // Display sizes in GB for large drives, MB for smaller ones
	            if (total_gb > 0) {
	                LOGNOTE("  Mounted: %s, %u GB total, %u GB free", fs_type, total_gb, free_gb);
	            } else {
	                LOGNOTE("  Mounted: %s, %u MB total, %u MB free", fs_type, total_mb, free_mb);
	            }
	            
	            // Get volume label
	            char label[12];
	            if (f_getlabel((const char*)drive, label, nullptr) == FR_OK && strlen(label) > 0) {
	                LOGNOTE("  Label: '%s'", label);
	            }
	            
	            // Show partition purpose
	            if (partition == 0) {
	                LOGNOTE("  Purpose: Boot partition (USBODE system files)");
	            } else if (partition == 1) {
	                LOGNOTE("  Purpose: Data partition (ISO images and user data)");
	            }
	            
	        } else {
	            if (partition == 0) {
	                LOGERR("  ERROR: Boot partition mount failed (error %d)", res);
	            } else {
	                LOGNOTE("  Not mounted or not present (error %d)", res);
	            }
	        }
	    }
	    
	    LOGNOTE("Partition scanning complete");

    // Check if setup is required BEFORE starting any services
    LOGNOTE("Checking if setup is required...");
    SetupStatus* setupStatus = SetupStatus::Get();
    
    // Use a more reliable partition check that matches the kernel scan
    bool partition1Exists = false;
    DWORD free_clusters;
    FATFS* fs;
    FRESULT res = f_getfree("1:", &free_clusters, &fs);
    if (res == FR_OK && fs != nullptr) {
        partition1Exists = true;
        LOGNOTE("Partition 1 verified as accessible via f_getfree");
    } else {
        LOGNOTE("Partition 1 not accessible via f_getfree (error %d)", res);
    }
    
    bool setupRequired = !partition1Exists;
    setupStatus->setSetupRequired(setupRequired);
    
    if (setupRequired) {
        LOGNOTE("Second partition not found - performing setup");
        
        if (!setupStatus->performSetup()) {
            LOGERR("Setup failed");
            return ShutdownHalt;
        }
        
        // Give a moment for the display to show completion
        CScheduler::Get()->MsSleep(2000);
        
        LOGNOTE("Rebooting device to complete setup...");
        setupStatus->setStatusMessage("Rebooting to complete setup...");
        
        // Trigger automatic reboot
        DeviceState::Get().setShutdownMode(ShutdownReboot);
        
        // IMPORTANT: Don't start services if we're rebooting
        LOGNOTE("Setup initiated reboot - skipping service startup");
    } else {
        LOGNOTE("Second partition exists - starting services normally");
        
        // Only start CDROM and SCSITB services if second partition exists
        // Initialize USB CD Service
        new CDROMService();
        LOGNOTE("Started CDROM service");

        // Load our SCSITB Service
        new SCSITBService();
        LOGNOTE("Started SCSITB service");
    }

    // Load our Display Service, if needed (this can run without second partition)
    const char* displayType = config->GetDisplayHat();
    if (strcmp(displayType, "none") != 0 ) {
        new DisplayService(displayType);
        LOGNOTE("Started DisplayService service");
    }

    } else { // Mass Storage Device Mode
	    // Start our SD Card Service
	    new SDCARDService(&m_EMMC);
	    LOGNOTE("Started SDCARD Service");
    }

    static const char ServiceName[] = HOSTNAME;
    CmDNSPublisher* pmDNSPublisher = nullptr;
    CWebServer* pCWebServer = nullptr;
    CFTPDaemon* m_pFTPDaemon = nullptr;

    // Previous IP tracking
    bool ntpInitialized = false;

    // Main Loop
    for (unsigned nCount = 0; 1; nCount++) {

        // Start the Web Server
        if (m_Net.IsRunning() && !pCWebServer) {
            // Create the web server
            pCWebServer = new CWebServer(&m_Net, &m_ActLED);

            LOGNOTE("Started Webserver service");
        }

        // Run NTP
        if (m_Net.IsRunning() && !ntpInitialized) {
                // Read timezone from config.txt
                const char* timezone = config->GetTimezone();

                // Initialize NTP with the timezone
                InitializeNTP(timezone);
                ntpInitialized = true;
        }

        // Publish mDNS
        if (m_Net.IsRunning() && !pmDNSPublisher) {
            static const char* ppText[] = {"path=/index.html", nullptr};
            pmDNSPublisher = new CmDNSPublisher(&m_Net);
            if (!pmDNSPublisher->PublishService(ServiceName, "_http._tcp", 80, ppText)) {
                LOGNOTE("Cannot publish service");
            }
            LOGNOTE("Started mDNS service");
        }

        // Start the FTP Server
        if (mode == 0 && m_Net.IsRunning() && !m_pFTPDaemon) {
            m_pFTPDaemon = new CFTPDaemon("cdrom", "cdrom");
            if (!m_pFTPDaemon->Initialize()) {
                LOGERR("Failed to init FTP daemon");
                delete m_pFTPDaemon;
                m_pFTPDaemon = nullptr;
            } else
                LOGNOTE("Started FTP service");
        }

        // Check if we should shutdown or halt
	if (DeviceState::Get().getShutdownMode() != ShutdownNone) {
		// Unmount & flush before we reboot or shutdown
		LOGNOTE("Flushing SD card writes");
		f_mount(0, DRIVE, 1);

		// Do it!
		if (DeviceState::Get().getShutdownMode() == ShutdownHalt) {
			LOGNOTE("Shut down - it's now safe to remove USBODE");
			return ShutdownHalt;
		} else {
			LOGNOTE("Rebooting...");
			return ShutdownReboot;
		}
	}

	// Give other tasks a chance to run
	m_Scheduler.Yield();

    }

    LOGNOTE("ShutdownHalt");
    return ShutdownHalt;
}

void CKernel::InitializeNTP(const char* timezone) {
    // Make sure network is running
    if (!m_Net.IsRunning()) {
        LOGERR("Network not running, NTP initialization skipped");
        return;
    }

    // Log that we're starting NTP synchronization
    LOGNOTE("Starting NTP time synchronization (using UTC/GMT)");

    // Create DNS client for resolving NTP server
    CDNSClient DNSClient(&m_Net);

    // NTP server address
    CIPAddress NTPServerIP;
    const char* NTPServer = "pool.ntp.org";

    // Resolve NTP server address
    LOGNOTE("Resolving NTP server: %s", NTPServer);
    if (!DNSClient.Resolve(NTPServer, &NTPServerIP)) {
        LOGERR("Cannot resolve NTP server: %s", NTPServer);
        return;
    }

    // Log the resolved IP
    CString IPString;
    NTPServerIP.Format(&IPString);
    LOGNOTE("NTP server resolved to: %s", (const char*)IPString);

    // Create NTP client
    CNTPClient NTPClient(&m_Net);

    // Get time from NTP server
    LOGNOTE("Requesting time from NTP server...");
    unsigned nTime = NTPClient.GetTime(NTPServerIP);
    if (nTime == 0) {
        LOGERR("NTP time synchronization failed");
        return;
    }

    // Set system time
    CTime Time;
    Time.Set(nTime);

    // Set time in the CTimer singleton for system-wide use
    CTimer::Get()->SetTime(nTime);

    // Log the current time (in UTC/GMT)
    LOGNOTE("Time synchronized successfully: %s (UTC/GMT)", Time.GetString());
}

CNetSubSystem* CKernel::GetNetwork() {
	return &m_Net;
}

FATFS* CKernel::GetFileSystem() {
	return &m_FileSystem;
}
