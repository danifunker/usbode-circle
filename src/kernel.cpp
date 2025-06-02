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
#include <string.h>

#define DRIVE "SD:"
#define FIRMWARE_PATH DRIVE "/firmware/"
#define SUPPLICANT_CONFIG_FILE DRIVE "/wpa_supplicant.conf"
#define CONFIG_FILE DRIVE "/config.txt"
#define LOG_FILE DRIVE "/logfile.txt"
#define HOSTNAME "CDROM"
#define SPI_MASTER_DEVICE  0

// Define the images directory
#define IMAGES_DIR "SD:/images"

// Static global pointer to the kernel instance (needed for the callback)
static CKernel* g_pKernel = nullptr;

LOGMODULE("kernel");

// Custom implementation of strrchr since it's not available in Circle
static const char* FindLastOccurrence(const char* str, int ch)
{
    if (str == nullptr)
        return nullptr;
        
    const char* last = nullptr;
    
    while (*str != '\0')
    {
        if (*str == ch)
            last = str;
        str++;
    }
    
    return last;
}

// Add this near other constant definitions at the top of the file
const char CKernel::ConfigOptionTimeZone[] = "timezone";

CKernel::CKernel(void)
: m_Screen(m_Options.GetWidth(), m_Options.GetHeight()),
  m_Timer(&m_Interrupt),
  m_Logger(m_Options.GetLogLevel(), &m_Timer),
  m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
  m_WLAN(FIRMWARE_PATH),
  m_Net(0, 0, 0, 0, HOSTNAME, NetDeviceTypeWLAN),
  m_WPASupplicant(SUPPLICANT_CONFIG_FILE),
  m_CDGadget(&m_Interrupt),
  m_pSPIMaster(nullptr),
  m_pDisplayManager(nullptr),
  m_pButtonManager(nullptr),
  m_ScreenState(ScreenStateMain),
  m_nCurrentISOIndex(0),
  m_nTotalISOCount(0),
  m_pISOList(nullptr)
{
    //m_ActLED.Blink(5);  // show we are alive
}

CKernel::~CKernel(void)
{
    // Clean up ISO list if allocated
    if (m_pISOList != nullptr)
    {
        delete[] m_pISOList;
        m_pISOList = nullptr;
    }
    
    if (m_pButtonManager != nullptr)
    {
        delete m_pButtonManager;
        m_pButtonManager = nullptr;
    }
    
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

        // If network initialization succeeded, read timezone from config.txt
        if (bOK && m_Net.IsRunning())
        {
            // Read timezone from config.txt
            CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
            if (Properties.Load())
            {
                Properties.SelectSection("usbode");
                CString Value;
                const char* timezone = Properties.GetString(ConfigOptionTimeZone, "UTC");
                
                // Initialize NTP with the timezone
                InitializeNTP(timezone);
            }
            else
            {
                // Use default timezone if config file not available
                InitializeNTP("UTC");
            }
        }
    
	return bOK;
}

TShutdownMode CKernel::Run(void)
{
    // Initialize the global kernel pointer
    g_pKernel = this;
    
    // Load our config file loader
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    if (!Properties.Load())
    {
        LOGERR("Error loading properties from %s (line %u)",
                CONFIG_FILE, Properties.GetErrorLine());
        return ShutdownHalt;
    }
    Properties.SelectSection("usbode");

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

    // Initialize USB CD gadget
    LOGNOTE("Starting USB CD gadget initialization");
    m_CDGadget.SetDevice(cueBinFileDevice);
    if (!m_CDGadget.Initialize())
    {
        LOGERR("Failed to initialize USB CD gadget");
        return ShutdownHalt;
    }
    LOGNOTE("USB CD gadget initialized successfully");

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
        
        // Allow some time for USB to stabilize before initializing buttons
        LOGNOTE("Waiting for USB to stabilize before initializing buttons");
        m_Scheduler.MsSleep(2000);
        
        // Initialize buttons AFTER USB CD and display are initialized
        InitializeButtons(displayTypeEnum);
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
		// Process button updates FIRST for best responsiveness
		if (m_pButtonManager != nullptr)
		{
			m_pButtonManager->Update();
			
			// OPTIMIZATION: Check for button updates more frequently during file selection
			// This makes the UI feel much more responsive when navigating file lists
			if (m_ScreenState == ScreenStateLoadISO) {
				// If we're in the file selection screen, check buttons again immediately
				m_pButtonManager->Update();
			}
		}
		
		// Then handle USB and network
		m_CDGadget.UpdatePlugAndPlay();
		m_CDGadget.Update();
		
		// CRITICAL: Process network tasks even in ISO selection mode
		if (m_Net.IsRunning()) 
		{
			m_Net.Process();
			
			// Check and maintain FTP and Web services
			if (pCWebServer == nullptr) {
				pCWebServer = new CWebServer(&m_Net, &m_CDGadget, &m_ActLED, &Properties);
				pCWebServer->SetDisplayUpdateHandler(DisplayUpdateCallback);
				LOGNOTE("Started Webserver");
			}
			
			if (m_pFTPDaemon == nullptr) {
				m_pFTPDaemon = new CFTPDaemon("cdrom", "cdrom");
				if (!m_pFTPDaemon->Initialize()) {
					LOGERR("Failed to init FTP daemon");
					delete m_pFTPDaemon;
					m_pFTPDaemon = nullptr;
				}
				else {
					LOGNOTE("FTP daemon initialized");
				}
			}
		}
		
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

		// Use shorter yielding for more responsive button checks
		// OPTIMIZATION: Yield less frequently when in file selection mode
		if (m_ScreenState != ScreenStateLoadISO || nCount % 10 == 0)
		{
			m_Scheduler.Yield();
		}
		
		// Status updates less frequently
		if (nCount % 100 == 0)  // Only update status occasionally
		{
			// Periodic status update
			unsigned currentTime = m_Timer.GetTicks();
			if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL && 
			    m_ScreenState != ScreenStateLoadISO) {  // Skip updates while in ISO selection screen
				// Get the current image name from properties
				Properties.SelectSection("usbode");
				const char* currentImage = Properties.GetString("current_image", "image.iso");
				
				// Update display with current image name ONLY if not in ISO selection
				if (m_ScreenState != ScreenStateLoadISO) {
					UpdateDisplayStatus(currentImage);
					lastStatusUpdate = currentTime;
				}
			}
		}
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
    
    // CRITICAL: Skip updates completely while in ISO selection screen
    if (m_ScreenState == ScreenStateLoadISO)
    {
        // Remove the debug log message here
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
    
    // Remove the excessive debug log here
    
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
            
        // Only log when the display actually changes
        LOGNOTE("Display updated: IP=%s, Image=%s", 
                (const char*)IPString, currentImage);
                
        // Store current values
        LastDisplayedIP = IPString;
        LastDisplayedImage = currentImage;
        LastUpdateTime = currentTime;
    }
}

void CKernel::ButtonEventHandler(unsigned nButtonIndex, boolean bPressed, void* pParam)
{
    CKernel* pKernel = static_cast<CKernel*>(pParam);
    if (pKernel == nullptr || pKernel->m_pButtonManager == nullptr)
    {
        return;
    }
    
    // For button presses - handle actions IMMEDIATELY for responsiveness
    if (bPressed)
    {
        // Handle specific actions based on current screen state
        switch (pKernel->m_ScreenState)
        {
            case ScreenStateMain:
                // On main screen, only KEY1 (button 5) should work to open ISO selection
                if (nButtonIndex == 5) { // KEY1 button
                    // Show a loading message before scanning for files
                    if (pKernel->m_pDisplayManager != nullptr)
                    {
                        pKernel->m_pDisplayManager->ShowStatusScreen(
                            "Please Wait",
                            "Opening Image Browser",
                            "Scanning files...");
                        pKernel->m_pDisplayManager->Refresh();
                    }
                    
                    // Immediate response to KEY1 button in main screen
                    pKernel->m_ScreenState = ScreenStateLoadISO;
                    pKernel->ScanForISOFiles();
                    pKernel->ShowISOSelectionScreen();
                }
                break;
                
            case ScreenStateLoadISO:
                // OPTIMIZATION: For ISO selection screen, skip intermediary "Navigating..." screens
                // to make button presses feel much more responsive
                
                if (nButtonIndex == 0) { // UP button - previous ISO (single step) with wrapping
                    if (pKernel->m_nTotalISOCount > 0) {
                        // Handle wrapping from first to last file
                        if (pKernel->m_nCurrentISOIndex == 0) {
                            pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 1;
                        } else {
                            pKernel->m_nCurrentISOIndex--;
                        }
                        pKernel->ShowISOSelectionScreen();
                    }
                }
                else if (nButtonIndex == 1) { // DOWN button - next ISO (single step) with wrapping
                    if (pKernel->m_nTotalISOCount > 0) {
                        // Handle wrapping from last to first file
                        if (pKernel->m_nCurrentISOIndex >= pKernel->m_nTotalISOCount - 1) {
                            pKernel->m_nCurrentISOIndex = 0;
                        } else {
                            pKernel->m_nCurrentISOIndex++;
                        }
                        pKernel->ShowISOSelectionScreen();
                    }
                }
                else if (nButtonIndex == 2) { // LEFT button - skip back 5 files with wrapping
                    if (pKernel->m_nTotalISOCount > 0) {
                        // Skip back 5 files with wrapping
                        if (pKernel->m_nCurrentISOIndex < 5) {
                            // Wrap around to the end
                            pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 
                                (5 - pKernel->m_nCurrentISOIndex);
                            
                            // Make sure we don't go out of bounds
                            if (pKernel->m_nCurrentISOIndex >= pKernel->m_nTotalISOCount) {
                                pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 1;
                            }
                        } else {
                            pKernel->m_nCurrentISOIndex -= 5;
                        }
                        pKernel->ShowISOSelectionScreen();
                    }
                }
                else if (nButtonIndex == 3) { // RIGHT button - skip forward 5 files with wrapping
                    if (pKernel->m_nTotalISOCount > 0) {
                        // Skip forward 5 files with wrapping
                        if (pKernel->m_nCurrentISOIndex + 5 >= pKernel->m_nTotalISOCount) {
                            // Wrap around to the beginning
                            pKernel->m_nCurrentISOIndex = 
                                (pKernel->m_nCurrentISOIndex + 5) % pKernel->m_nTotalISOCount;
                        } else {
                            pKernel->m_nCurrentISOIndex += 5;
                        }
                        pKernel->ShowISOSelectionScreen();
                    }
                }
                else if (nButtonIndex == 5) { // KEY1 button - load selected ISO
                    // Show loading message
                    if (pKernel->m_pDisplayManager != nullptr) {
                        const char* selectedFile = 
                            (pKernel->m_nTotalISOCount > 0 && pKernel->m_pISOList != nullptr) ?
                            (const char*)pKernel->m_pISOList[pKernel->m_nCurrentISOIndex] : "Unknown";
                            
                        pKernel->m_pDisplayManager->ShowStatusScreen(
                            "Please Wait",
                            "Loading Image:",
                            selectedFile);
                        pKernel->m_pDisplayManager->Refresh();
                    }
                    
                    pKernel->LoadSelectedISO();
                    pKernel->m_ScreenState = ScreenStateMain;
                    // Update main screen after loading ISO
                    pKernel->UpdateDisplayStatus(nullptr);
                }
                else if (nButtonIndex == 6) { // KEY2 button - cancel and return to main
                    pKernel->m_ScreenState = ScreenStateMain;
                    pKernel->UpdateDisplayStatus(nullptr);
                }
                break;
        }
    }
}

void CKernel::InitializeButtons(TDisplayType displayType)
{
    // Early exit if no display type is specified
    if (displayType == DisplayTypeUnknown)
    {
        LOGNOTE("No display configured, skipping button initialization");
        return;
    }
    
    // Skip if buttons are already initialized
    if (m_pButtonManager != nullptr)
    {
        LOGNOTE("Buttons already initialized");
        return;
    }
    
    LOGNOTE("Starting button initialization for display type: %s", 
            displayType == DisplayTypeSH1106 ? "SH1106" : 
            displayType == DisplayTypeST7789 ? "ST7789" : "Unknown");
    
    // Create the button manager
    m_pButtonManager = new CGPIOButtonManager(&m_Logger, displayType);
    if (m_pButtonManager == nullptr)
    {
        LOGERR("Failed to create button manager");
        return;
    }
    
    // Initialize the button manager
    if (!m_pButtonManager->Initialize())
    {
        LOGERR("Failed to initialize button manager");
        delete m_pButtonManager;
        m_pButtonManager = nullptr;
        return;
    }
    
    // Register the button event handler
    m_pButtonManager->RegisterEventHandler(ButtonEventHandler, this);
    
    LOGNOTE("Button initialization complete - %u buttons configured", 
            m_pButtonManager->GetButtonCount());
}

void CKernel::ScanForISOFiles(void)
{
    // Show loading indicator immediately before starting the operation
    if (m_pDisplayManager != nullptr)
    {
        m_pDisplayManager->ShowStatusScreen(
            "Please Wait",
            "Scanning for ISOs...",
            "This may take a moment");
        
        // Ensure the display is updated immediately
        m_pDisplayManager->Refresh();
    }
    
    // Clean up previous list
    if (m_pISOList != nullptr)
    {
        delete[] m_pISOList;
        m_pISOList = nullptr;
    }
    
    // Reset counters
    m_nTotalISOCount = 0;
    m_nCurrentISOIndex = 0;
    
    // Allocate with larger MAX_ISO_FILES value
    m_pISOList = new CString[MAX_ISO_FILES];
    if (m_pISOList == nullptr)
    {
        LOGERR("Failed to allocate memory for ISO list");
        return;
    }
    
    // Get current ISO from config just once
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");
    bool currentImageFound = false;
    
    // First try to scan the images directory
    DIR Directory;
    FILINFO FileInfo;
    
    FRESULT result = f_opendir(&Directory, IMAGES_DIR);
    if (result == FR_OK)
    {
        LOGNOTE("Scanning for ISO files in %s", IMAGES_DIR);
        
        // Read all files in this directory
        while (f_readdir(&Directory, &FileInfo) == FR_OK && FileInfo.fname[0] != 0)
        {
            // Skip directories
            if (FileInfo.fattrib & AM_DIR)
                continue;
            
            // Check for .iso, .cue, or .bin extensions
            const char* Extension = FindLastOccurrence(FileInfo.fname, '.');
            if (Extension == nullptr)
                continue;
                
            if (strcasecmp(Extension, ".iso") == 0 || 
                strcasecmp(Extension, ".cue") == 0 || 
                strcasecmp(Extension, ".bin") == 0)
            {
                // Check if we have space left
                if (m_nTotalISOCount >= MAX_ISO_FILES)
                {
                    LOGWARN("Maximum ISO file count reached (%u)", MAX_ISO_FILES);
                    break;
                }
                
                // Add to our list
                m_pISOList[m_nTotalISOCount] = FileInfo.fname;
                
                // Check if this is the current image
                if (strcasecmp(FileInfo.fname, currentImage) == 0)
                {
                    m_nCurrentISOIndex = m_nTotalISOCount;
                    currentImageFound = true;
                }
                
                m_nTotalISOCount++;
            }
        }
        
        f_closedir(&Directory);
    }
    
    // Sort files alphabetically using a more efficient algorithm
    for (unsigned i = 0; i < m_nTotalISOCount - 1; i++)
    {
        unsigned minIndex = i;
        
        for (unsigned j = i + 1; j < m_nTotalISOCount; j++)
        {
            if (strcasecmp((const char*)m_pISOList[j], (const char*)m_pISOList[minIndex]) < 0)
            {
                minIndex = j;
            }
        }
        
        if (minIndex != i)
        {
            // Swap
            CString Temp = m_pISOList[i];
            m_pISOList[i] = m_pISOList[minIndex];
            m_pISOList[minIndex] = Temp;
            
            // Update current index if affected
            if (m_nCurrentISOIndex == i)
                m_nCurrentISOIndex = minIndex;
            else if (m_nCurrentISOIndex == minIndex)
                m_nCurrentISOIndex = i;
        }
    }
    
    LOGNOTE("Found %u ISO/CUE/BIN files, current is %u (%s)", 
            m_nTotalISOCount, 
            m_nCurrentISOIndex, 
            m_nTotalISOCount > 0 ? (const char*)m_pISOList[m_nCurrentISOIndex] : "none");
}

void CKernel::ShowISOSelectionScreen(void)
{
    if (m_pDisplayManager == nullptr)
    {
        return;
    }
    
    // Get current ISO from config
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");
    
    if (m_nTotalISOCount == 0)
    {
        // No ISO files found
        m_pDisplayManager->ShowStatusScreen(
            "Select Image",
            "No Images files found",
            "Place files on SD card");
    }
    else
    {
        // Display current file in the selection
        const char* selectedFile = (const char*)m_pISOList[m_nCurrentISOIndex];
        
        // Pass both current and selected ISO
        m_pDisplayManager->ShowFileSelectionScreen(
            currentImage,  // Currently loaded ISO
            selectedFile,  // Currently selected ISO in the list
            m_nCurrentISOIndex + 1,
            m_nTotalISOCount);
    }
}

void CKernel::LoadSelectedISO(void)
{
    // Early validation checks...
    if (m_nTotalISOCount == 0 || m_pISOList == nullptr)
    {
        LOGERR("No ISO files available");
        return;
    }
    
    // Get the selected ISO filename
    const char* SelectedISO = (const char*)m_pISOList[m_nCurrentISOIndex];
    
    // CRITICAL CHANGE: Don't construct the full path here, 
    // just pass the filename to loadCueBinFileDevice
    LOGNOTE("Loading ISO: %s", SelectedISO);
    
    // Let loadCueBinFileDevice handle the path construction
    CCueBinFileDevice* CueBinFileDevice = loadCueBinFileDevice(SelectedISO);
    if (CueBinFileDevice == nullptr)
    {
        LOGERR("Failed to load Image: %s", SelectedISO);
        
        // Get the current image that's still loaded
        CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
        Properties.Load();
        Properties.SelectSection("usbode");
        const char* currentImage = Properties.GetString("current_image", "image.iso");
        
        // Show error message with current loaded image
        if (m_pDisplayManager != nullptr)
        {
            m_pDisplayManager->ShowStatusScreen(
                "Error loading Image",
                "Failed to load file",
                currentImage);  // Show currently loaded ISO
        }
        
        return;
    }
    
    // Update the config file only after successful load
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    Properties.SetString("current_image", SelectedISO);
    Properties.Save();
    
    LOGNOTE("Selected new Image: %s", SelectedISO);
    
    // Set the new device in the CD gadget
    m_CDGadget.SetDevice(CueBinFileDevice);
    
    // Update the display
    UpdateDisplayStatus(SelectedISO);
}

void CKernel::InitializeNTP(const char* timezone)
{
    // Make sure network is running
    if (!m_Net.IsRunning())
    {
        LOGERR("Network not running, NTP initialization skipped");
        return;
    }

    // Log the timezone
    LOGNOTE("Setting timezone: %s", timezone);

    // Create DNS client for resolving NTP server
    CDNSClient DNSClient(&m_Net);
    
    // NTP server address
    CIPAddress NTPServerIP;
    const char* NTPServer = "pool.ntp.org";
    
    // Resolve NTP server address
    if (!DNSClient.Resolve(NTPServer, &NTPServerIP))
    {
        LOGERR("Cannot resolve NTP server: %s", NTPServer);
        return;
    }
    
    // Create NTP client
    CNTPClient NTPClient(&m_Net);
    
    // Get time from NTP server
    unsigned nTime = NTPClient.GetTime(NTPServerIP);
    if (nTime == 0)
    {
        LOGERR("NTP time synchronization failed");
        return;
    }
    
    // Set system time
    CTime Time;
    Time.Set(nTime);
    
    // Log the current time
    LOGNOTE("Time synchronized: %s", Time.GetString());
}
