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
#include <circle/memory.h>

#include <circle/time.h>

#define ROOTDRIVE "0:"
#define IMAGESDRIVE "1:"
#define FIRMWARE_PATH ROOTDRIVE "/firmware/"
#define SUPPLICANT_CONFIG_FILE ROOTDRIVE "/wpa_supplicant.conf"
#define CONFIG_FILE ROOTDRIVE "/config.txt"
#define LOG_FILE ROOTDRIVE "/logfile.txt"
#define HOSTNAME "usbode"
#define SPI_MASTER_DEVICE 0

// Define the images directory
#define IMAGES_DIR IMAGESDRIVE "/"

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
    //but do we really care? 
    //We're shutting down or rebooting anyway
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
        if (f_mount(&m_RootFileSystem, ROOTDRIVE, 1) != FR_OK) {
            LOGERR("Cannot mount drive: %s", ROOTDRIVE);

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
        LOGNOTE("Initialized WPA supplicant");
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

    // Start the file logging service
    const char* logfile = config->GetLogfile();
    if (logfile) {
        new CFileLogDaemon(logfile);
        LOGNOTE("Started the Log File service");
    }

    // Announce ourselves as early as possible
    LOGNOTE("=====================================");
    LOGNOTE("Welcome to USBODE");
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);
    LOGNOTE("Git Info: %s @ %s", GIT_BRANCH, GIT_COMMIT);
    LOGNOTE("Memory Size: %u", CMemorySystem::Get()->GetMemSize());
    LOGNOTE("=====================================");

    // Initialize SetupStatus
    SetupStatus::Init(&m_EMMC);
    LOGNOTE("Initialized SetupStatus"); 

    // Do we need to do first time setup?
    if (SetupStatus::Get()->isSetupRequired()) {
	    // Start Display Service for setup screen
	    const char* displayType = config->GetDisplayHat();
	    if (strcmp(displayType, "none") != 0) {
		new DisplayService(displayType);
		LOGNOTE("Started DisplayService service");
		
		// Give the display a moment to initialize
		CScheduler::Get()->MsSleep(500);
	    }

	    if (SetupStatus::Get()->performSetup()) {
		    LOGNOTE("Setup successful, rebooting");
		    // Give a moment for the display to show completion
		    CScheduler::Get()->MsSleep(5000);
		    return ShutdownReboot;
	    } else {
		    LOGERR("Setup failed, shutting down");
		    return ShutdownHalt;
	    }
    }

    // Clean up SetupStatus
    SetupStatus::Shutdown();

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

    // Mount images partition for normal operation
    if (f_mount(&m_ImagesFileSystem, IMAGESDRIVE, 1) != FR_OK) {
        LOGERR("Failed to mount images partition %s", IMAGESDRIVE);
        return ShutdownHalt;
    }
    LOGNOTE("Partition 1 (data/images) mounted successfully");
        
    // Start services that depend on both partitions
    new CDROMService();
    LOGNOTE("Started CDROM service");

    new SCSITBService();
    LOGNOTE("Started SCSITB service");

    // Start display services for normal running mode
    const char* displayType = config->GetDisplayHat();
    if (strcmp(displayType, "none") != 0) {
	    new DisplayService(displayType);
            LOGNOTE("Started DisplayService service");
    }

    static const char ServiceName[] = HOSTNAME;
    //CmDNSPublisher* pmDNSPublisher = nullptr;
    CWebServer* pCWebServer = nullptr;
    CFTPDaemon* m_pFTPDaemon = nullptr;

    // Previous IP tracking
    bool ntpInitialized = false;

    // Main Loop
    //int counter = 0;
    for (;;) {

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
	/* Disabled for now until but is fixed
        if (m_Net.IsRunning() && !pmDNSPublisher) {
            static const char* ppText[] = {"path=/index.html", nullptr};
            pmDNSPublisher = new CmDNSPublisher(&m_Net);
            if (!pmDNSPublisher->PublishService(ServiceName, "_http._tcp", 80, ppText)) {
                LOGNOTE("Cannot publish service");
            }
            LOGNOTE("Started mDNS service");
        }
	*/

        // Start the FTP Server
        if (m_Net.IsRunning() && !m_pFTPDaemon) {
            m_pFTPDaemon = new CFTPDaemon("cdrom", "cdrom");
            if (!m_pFTPDaemon->Initialize()) {
                LOGERR("Failed to init FTP daemon");
                delete m_pFTPDaemon;
                m_pFTPDaemon = nullptr;
            } else {
                LOGNOTE("Started FTP service");
	    }
        }

        // Check if we should shutdown or halt
	if (DeviceState::Get().getShutdownMode() != ShutdownNone) {
		// Unmount & flush before we reboot or shutdown
		LOGNOTE("Flushing SD card writes");
		f_mount(0, ROOTDRIVE, 1);
		f_mount(0, IMAGESDRIVE, 1);

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
	//CScheduler::Get()->MsSleep(100);

	/*
	if (counter >= 100) {
            counter = 0;    // reset counter
	    CMemorySystem::DumpStatus();
    	    LOGDBG("Heap Free Memory %u", CMemorySystem::Get()->GetHeapFreeSpace(HEAP_ANY));
        }
	counter++;
	*/

    }

    // Unreachable
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

/*
FATFS* CKernel::GetFileSystem() {
	return &m_FileSystem;
}
*/
