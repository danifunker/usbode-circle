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
#include "util.h"
#include "webserver.h"

#define DRIVE "SD:"
#define FIRMWARE_PATH DRIVE "/firmware/"
#define SUPPLICANT_CONFIG_FILE DRIVE "/wpa_supplicant.conf"
#define CONFIG_FILE DRIVE "/config.txt"
#define LOG_FILE DRIVE "/logfile.txt"
#define HOSTNAME "CDROM"
#define SPI_MASTER_DEVICE  0

// Static global pointer to the kernel instance (needed for the callback)
static CKernel* g_pKernel = nullptr;

LOGMODULE("kernel");

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
    m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
    m_WLAN (FIRMWARE_PATH),
    m_Net (0, 0, 0, 0, HOSTNAME, NetDeviceTypeWLAN),
    m_WPASupplicant (SUPPLICANT_CONFIG_FILE),
    m_CDGadget (&m_Interrupt),
    m_pSPIMaster(nullptr),
    m_pDisplayManager(nullptr)
{
	//m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
    if (m_pDisplayManager != nullptr)
    {
        delete m_pDisplayManager;
        m_pDisplayManager = nullptr;
    }
    
    if (m_pSPIMaster != nullptr)
    {
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
    }
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
		LOGNOTE("Initialized screen");
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
		LOGNOTE("Initialized serial");
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
		LOGNOTE("Initialized logger");
	}


	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
		LOGNOTE("Initialized interrupts");
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
		LOGNOTE("Initialized timer");
	}

	if (bOK)
	{
		bOK = m_EMMC.Initialize ();
		LOGNOTE("Initialized eMMC");
	}

	if (bOK)
        {
                if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
                {
                        LOGERR("Cannot mount drive: %s", DRIVE);

                        bOK = FALSE;
                }
		LOGNOTE("Initialized filesystem");
        }

	if (bOK)
        {
                bOK = m_WLAN.Initialize ();
		LOGNOTE("Initialized WLAN");
        }

        if (bOK)
        {
                bOK = m_Net.Initialize (FALSE);
			LOGNOTE("Initialized network");
        }

        if (bOK)
        {
                bOK = m_WPASupplicant.Initialize ();
		LOGNOTE("Initialized WAP supplicant");
        }

	return bOK;
}

TShutdownMode CKernel::Run (void)
{

	// Initialize the global kernel pointer
    g_pKernel = this;
    
	// Load our config file loader
	CPropertiesFatFsFile Properties (CONFIG_FILE, &m_FileSystem);
	if (!Properties.Load ())
	{
		LOGERR("Error loading properties from %s (line %u)",
				CONFIG_FILE, Properties.GetErrorLine ());
		return ShutdownHalt;
	}
	Properties.SelectSection ("usbode");

    // Start the file logging daemon
    const char *logfile = Properties.GetString("logfile", nullptr);
    if (logfile) {
        new CFileLogDaemon(logfile);
        LOGNOTE("Started log file daemon");
    }

    LOGNOTE("=====================================");
    LOGNOTE("Welcome to USBODE");
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);
    LOGNOTE("=====================================");

    // Load our current disc image
    const char *imageName = Properties.GetString("current_image", "image.iso");
    LOGNOTE("Found image filename %s", imageName);

    CCueBinFileDevice *cueBinFileDevice = loadCueBinFileDevice(imageName);
    if (!cueBinFileDevice) {
        LOGERR("Failed to load cueBinFileDevice %s", imageName);
        return ShutdownHalt;
    }

    m_CDGadget.SetDevice(cueBinFileDevice);
    m_CDGadget.Initialize();

	// Display configuration
	const char* displayType = Properties.GetString("displayhat", "none");
	LOGNOTE("Display hat configured: %s", displayType);
    
    // Initialize the appropriate display type
    TDisplayType displayTypeEnum = GetDisplayTypeFromString(displayType);
    
    if (displayTypeEnum != DisplayTypeUnknown)
    {
        // Initialize display based on type
        InitializeDisplay(displayTypeEnum);
        
        // If display was initialized successfully
        if (m_pDisplayManager != nullptr)
        {
            // Show status screen with current information
            CString IPString;
            if (m_Net.IsRunning())
            {
                m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
            }
            else
            {
                IPString = "Not connected";
            }
            
            m_pDisplayManager->ShowStatusScreen(
                "USBODE v2.00-pre1",
                (const char*)IPString,
                imageName);
        }
    }

	bool showIP = true;
	static const char ServiceName[] = HOSTNAME;
	static const char *ppText[] = {"path=/index.html", nullptr};
	CmDNSPublisher *pmDNSPublisher = nullptr;
	CWebServer *pCWebServer = nullptr;
	CFTPDaemon *m_pFTPDaemon = nullptr;

	// Previous IP tracking
	static CString PreviousIPString = "";

	// Status update timing
	static unsigned lastStatusUpdate = 0;
	const unsigned STATUS_UPDATE_INTERVAL = 30000; // Update every 30 seconds

	for (unsigned nCount = 0; 1; nCount++)
	{
		// must be called from TASK_LEVEL to allow I/O operations
		m_CDGadget.UpdatePlugAndPlay ();
		m_CDGadget.Update ();

		// Show details of the network connection
		if (m_Net.IsRunning()) {
            CString CurrentIPString;
            m_Net.GetConfig()->GetIPAddress()->Format(&CurrentIPString);
            
            // If IP changed (including from not connected to connected)
            if (CurrentIPString != PreviousIPString) {
                PreviousIPString = CurrentIPString;
                
                // Log network info
                if (showIP) {
                    showIP = false;
                    LOGNOTE("==========================================");
                    m_WLAN.DumpStatus();
                    LOGNOTE("Our IP address is %s", (const char*)CurrentIPString);
                    LOGNOTE("==========================================");
                }
                
                // Update display with new IP
                UpdateDisplayStatus(imageName);
            }
        }

		// Publish mDNS
		if (m_Net.IsRunning() && pmDNSPublisher == nullptr) {
			pmDNSPublisher = new CmDNSPublisher (&m_Net);
			if (!pmDNSPublisher->PublishService (ServiceName, "_http._tcp", 5004, ppText))
			{
				LOGNOTE ("Cannot publish service");
			}
			LOGNOTE("Published mDNS");
		}

		// Start the Web Server
		if (m_Net.IsRunning() && pCWebServer == nullptr) {
			pCWebServer = new CWebServer(&m_Net, &m_CDGadget, &m_ActLED, &Properties);
			pCWebServer->SetDisplayUpdateHandler(DisplayUpdateCallback);
			LOGNOTE("Started Webserver");
                }

		// Start the FTP Server
		if (m_Net.IsRunning() && !m_pFTPDaemon)
		{
			m_pFTPDaemon = new CFTPDaemon("cdrom", "cdrom");
			if (!m_pFTPDaemon->Initialize())
			{
				LOGERR("Failed to init FTP daemon");
				delete m_pFTPDaemon;
				m_pFTPDaemon = nullptr;
			}
			else
				LOGNOTE("FTP daemon initialized");
		 }

        // Check for shutdown/reboot request from the web interface
        if (pCWebServer != nullptr) {
            TShutdownMode mode = pCWebServer->GetShutdownMode();
            if (mode != ShutdownNone) {
                LOGNOTE("Shutdown requested via web interface: %s",
                        (mode == ShutdownReboot) ? "Reboot" : "Halt");

                // Clean up resources
                delete pmDNSPublisher;
                delete pCWebServer;
                if (m_pFTPDaemon) {
                    delete m_pFTPDaemon;
                }

                return mode;
            }
        }

        m_Scheduler.Yield();

		// Periodic status update
		unsigned currentTime = m_Timer.GetTicks();
		if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
			// Get the current image name from properties
			Properties.SelectSection("usbode");
			const char* currentImage = Properties.GetString("current_image", "image.iso");
			
			// Update display with current image name
			UpdateDisplayStatus(currentImage);
			lastStatusUpdate = currentTime;
		}

		// Stop spinning
                //m_Scheduler.MsSleep(10);
	}

	LOGNOTE("ShutdownHalt");
	return ShutdownHalt;
}

// Static callback implementation (outside of any function)
void CKernel::DisplayUpdateCallback(const char* imageName)
{
    // Use the global kernel pointer
    if (g_pKernel != nullptr)
    {
        g_pKernel->UpdateDisplayStatus(imageName);
    }
}

TDisplayType CKernel::GetDisplayTypeFromString(const char* displayType)
{
    if (displayType == nullptr || strcmp(displayType, "none") == 0)
    {
        return DisplayTypeUnknown;
    }
    else if (strcmp(displayType, "pirateaudiolineout") == 0)
    {
        return DisplayTypeST7789;
    }
    else if (strcmp(displayType, "waveshare") == 0)
    {
        return DisplayTypeSH1106;
    }
    
    // Default to unknown
    return DisplayTypeUnknown;
}

void CKernel::InitializeDisplay(TDisplayType displayType)
{
    // Early exit if no display configured
    if (displayType == DisplayTypeUnknown)
    {
        LOGNOTE("No display configured");
        return;
    }
    
    // Initialize the appropriate SPI settings based on display type
    if (displayType == DisplayTypeSH1106)
    {
        LOGNOTE("Initializing SPI for SH1106 display");
        m_pSPIMaster = new CSPIMaster(
            CSH1106Display::SPI_CLOCK_SPEED,
            CSH1106Display::SPI_CPOL,
            CSH1106Display::SPI_CPHA,
            SPI_MASTER_DEVICE
        );
    }
    else if (displayType == DisplayTypeST7789)
    {
        LOGNOTE("Initializing SPI for ST7789 display");
        // Use ST7789-specific SPI settings when they're defined
        m_pSPIMaster = new CSPIMaster(
            CST7789Display::DEFAULT_SPI_CLOCK_SPEED,
            CST7789Display::DEFAULT_SPI_CPOL,
            CST7789Display::DEFAULT_SPI_CPHA,
            SPI_MASTER_DEVICE
        );
    }
    
    // Initialize the SPI master
    if (m_pSPIMaster == nullptr || !m_pSPIMaster->Initialize())
    {
        LOGERR("Failed to initialize SPI master for display");
        if (m_pSPIMaster != nullptr)
        {
            delete m_pSPIMaster;
            m_pSPIMaster = nullptr;
        }
        return;
    }
    
    // Create and initialize the display manager
    m_pDisplayManager = new CDisplayManager(&m_Logger, displayType);
    if (m_pDisplayManager == nullptr)
    {
        LOGERR("Failed to create display manager");
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
        return;
    }
    
    // Initialize the display
    if (!m_pDisplayManager->Initialize(m_pSPIMaster))
    {
        LOGERR("Failed to initialize display");
        delete m_pDisplayManager;
        m_pDisplayManager = nullptr;
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
        return;
    }
    
    LOGNOTE("Display initialized successfully");
}

void CKernel::UpdateDisplayStatus(const char* imageName)
{
    // Only update if display manager is initialized
    if (m_pDisplayManager == nullptr)
    {
        return;
    }
    
    // Track the last displayed image and IP to prevent redundant updates
    static CString LastDisplayedIP = "";
    static CString LastDisplayedImage = "";
    static unsigned LastUpdateTime = 0;
    
    // Debounce display updates - prevent multiple updates within a short time window
    unsigned currentTime = m_Timer.GetTicks();
    if (currentTime - LastUpdateTime < 500) // 500ms debounce time
    {
        return;
    }
    
    // Always load the current image from properties to ensure consistency
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");
    
    // Log for debugging
    LOGNOTE("UpdateDisplayStatus - Current image in properties: %s, passed image: %s", 
            currentImage, imageName ? imageName : "NULL");
    
    // Get current IP address
    CString IPString;
    if (m_Net.IsRunning())
    {
        m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
    }
    else
    {
        IPString = "Not connected";
    }
    
    // Only update if something has changed
    if (IPString != LastDisplayedIP || CString(currentImage) != LastDisplayedImage)
    {
        // Update the status screen
        m_pDisplayManager->ShowStatusScreen(
            "USBODE v2.00-pre1",
            (const char*)IPString,
            currentImage);  // Use the image from properties file
            
        LOGNOTE("Display status updated: IP=%s, Image=%s", 
                (const char*)IPString, currentImage);
                
        // Store current values
        LastDisplayedIP = IPString;
        LastDisplayedImage = currentImage;
        LastUpdateTime = currentTime;
    }
}
