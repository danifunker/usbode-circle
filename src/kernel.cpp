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
		// must be called from TASK_LEVEL to allow I/O operations
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
		
		// Add button update call with shorter processing time
		if (m_pButtonManager != nullptr)
		{
			m_pButtonManager->Update();
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

        m_Scheduler.Yield();

		// Periodic status update - but not when in ISO selection mode
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
    
    // For button presses - handle some actions immediately for responsiveness
    if (bPressed)
    {
        // In ISO selection screen, respond to up/down on press for better responsiveness
        if (pKernel->m_ScreenState == ScreenStateLoadISO)
        {
            switch (nButtonIndex)
            {
                case 0: // UP button
                    if (pKernel->m_nTotalISOCount > 0)
                    {
                        if (pKernel->m_nCurrentISOIndex > 0)
                            pKernel->m_nCurrentISOIndex--;
                        else
                            pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 1;
                            
                        pKernel->ShowISOSelectionScreen();
                    }
                    return; // Skip the release handling for this button
                    
                case 1: // DOWN button
                    if (pKernel->m_nTotalISOCount > 0)
                    {
                        pKernel->m_nCurrentISOIndex = (pKernel->m_nCurrentISOIndex + 1) % pKernel->m_nTotalISOCount;
                        pKernel->ShowISOSelectionScreen();
                    }
                    return; // Skip the release handling for this button
            }
        }
    }
    
    // For button releases - handle the rest of the actions
    if (!bPressed)  
    {
        // Less aggressive debounce for release actions (100ms instead of 150ms)
        static unsigned nLastReleaseTime = 0;
        unsigned nCurrentTime = pKernel->m_Timer.GetTicks();
        
        if (nCurrentTime - nLastReleaseTime < 100)
        {
            return;
        }
        
        nLastReleaseTime = nCurrentTime;
        
        const char* buttonLabel = pKernel->m_pButtonManager->GetButtonLabel(nButtonIndex);
        LOGNOTE("Button released: %s (index %u)", buttonLabel, nButtonIndex);
        
        // Flash the activity LED briefly
        pKernel->m_ActLED.On();
        
        // Handle based on screen state
        switch (pKernel->m_ScreenState)
        {
            case ScreenStateMain:
                // Main screen button handling
                switch (nButtonIndex)
                {
                    case 0: // SELECT button - Enter ISO selection
                        LOGNOTE("SELECT button - Entering ISO selection");
                        
                        // Stop any ongoing network services before switching modes
                        // if (pKernel->m_Net.IsRunning())
                        // {
                        //     LOGNOTE("Stopping network services");
                        //     pKernel->m_Net.Stop();
                        // }
                        
                        // Switch to ISO selection screen
                        pKernel->m_ScreenState = ScreenStateLoadISO;
                        pKernel->ShowISOSelectionScreen();
                        break;
                        
                    // case 1: // START button - Reboot system
                    //     LOGNOTE("START button - Rebooting system");
                    //     pKernel->m_ActLED.Blink(3, 100);  // Indicate rebooting
                    //     mbox_set_reboot();
                    //     break;
                        
                    case 5: // KEY1 button - ISO selection (without stopping network)
                        LOGNOTE("KEY1 button - Entering ISO selection");
                        
                        // Change screen state
                        pKernel->m_ScreenState = ScreenStateLoadISO;
                        
                        // Show loading screen for feedback
                        if (pKernel->m_pDisplayManager != nullptr)
                        {
                            pKernel->m_pDisplayManager->ShowStatusScreen(
                                "Loading ISO List",
                                "Please wait...",
                                "");
                        }
                        
                        // Scan for ISO files and display selection screen
                        pKernel->ScanForISOFiles();
                        pKernel->ShowISOSelectionScreen();
                        break;
                        
                    case 6: // KEY2 button - Advanced menu (for system operations)
                        LOGNOTE("KEY2 button - Entering advanced menu - Currently disabled");
                        // pKernel->m_ScreenState = ScreenStateAdvanced;
                        // pKernel->ShowAdvancedMenu();
                        break;
                        
                    // Add more cases for other buttons as needed
                }
                break;
                
            case ScreenStateLoadISO:
                // ISO selection screen button handling
                switch (nButtonIndex)
                {
                    // UP/DOWN handled on button press
                    
                    case 5: // KEY1 button - Select ISO (OK)
                        LOGNOTE("KEY1 button in ISO screen - Selecting ISO");
                        pKernel->LoadSelectedISO();
                        pKernel->m_ScreenState = ScreenStateMain;
                        break;
                        
                    case 6: // KEY2 button - Cancel selection
                        LOGNOTE("KEY2 button in ISO screen - Cancel selection");
                        pKernel->m_ScreenState = ScreenStateMain;
                        pKernel->UpdateDisplayStatus(nullptr);
                        break;
                        
                    // Legacy handling for SELECT/BACK buttons (buttons 2/3)
                    case 2: // SELECT button - Select current ISO
                        LOGNOTE("SELECT button - Selecting ISO");
                        pKernel->LoadSelectedISO();
                        pKernel->m_ScreenState = ScreenStateMain;
                        break;
                        
                    case 3: // BACK button - Return to main menu
                        LOGNOTE("BACK button - Return to main");
                        pKernel->m_ScreenState = ScreenStateMain;
                        pKernel->UpdateDisplayStatus(nullptr);
                        break;
                }
                break;

            // Add more cases for other screen states as needed            
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
    // Clean up previous list
    
    // Allocate new list with proper size
    m_pISOList = new CString[MAX_ISO_FILES];
    if (m_pISOList == nullptr)
    {
        LOGERR("Failed to allocate memory for ISO list");
        m_nTotalISOCount = 0;
        return;
    }
    
    // Reset counters
    m_nTotalISOCount = 0;
    m_nCurrentISOIndex = 0;
    
    // Try different paths
    const char* searchPaths[] = { IMAGES_DIR, DRIVE };
    
    // Get current ISO from config
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");
    bool currentImageFound = false;
    
    // First pass - scan both directories to collect all ISO files
    for (unsigned pathIndex = 0; pathIndex < sizeof(searchPaths)/sizeof(searchPaths[0]); pathIndex++)
    {
        DIR Directory;
        FILINFO FileInfo;
        
        // Open directory
        FRESULT result = f_opendir(&Directory, searchPaths[pathIndex]);
        if (result != FR_OK)
        {
            LOGWARN("Cannot open directory: %s", searchPaths[pathIndex]);
            continue;
        }
        
        LOGNOTE("Scanning for ISO files in %s", searchPaths[pathIndex]);
        
        // Read all files in this directory
        while (f_readdir(&Directory, &FileInfo) == FR_OK && FileInfo.fname[0] != 0)
        {
            // Skip directories
            if (FileInfo.fattrib & AM_DIR)
            {
                continue;
            }
            
            // Check for .iso, .cue, or .bin extensions
            const char* Extension = FindLastOccurrence(FileInfo.fname, '.');
            if (Extension != nullptr && 
                (strcasecmp(Extension, ".iso") == 0 || 
                 strcasecmp(Extension, ".cue") == 0 || 
                 strcasecmp(Extension, ".bin") == 0))
            {
                // Check if we have space left
                if (m_nTotalISOCount >= MAX_ISO_FILES)
                {
                    LOGWARN("Maximum ISO file count reached (%u), some files will be omitted", MAX_ISO_FILES);
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
        
        // If we found the current image, no need to search in the other directory
        if (currentImageFound)
        {
            break;
        }
    }
    
    // Sort the files alphabetically
    for (unsigned i = 0; i < m_nTotalISOCount - 1; i++)
    {
        for (unsigned j = i + 1; j < m_nTotalISOCount; j++)
        {
            if (strcasecmp((const char*)m_pISOList[i], (const char*)m_pISOList[j]) > 0)
            {
                // Swap
                CString Temp = m_pISOList[i];
                m_pISOList[i] = m_pISOList[j];
                m_pISOList[j] = Temp;
                
                // Update current index if affected
                if (m_nCurrentISOIndex == i)
                {
                    m_nCurrentISOIndex = j;
                }
                else if (m_nCurrentISOIndex == j)
                {
                    m_nCurrentISOIndex = i;
                }
            }
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
    
    // Get the selected ISO filename
    const char* SelectedISO = (const char*)m_pISOList[m_nCurrentISOIndex];
    
    // Construct full path - FIXED to avoid path duplication
    char FullPath[256];
    
    // Check if we're using images directory 
    DIR Directory;
    FRESULT result = f_opendir(&Directory, IMAGES_DIR);
    f_closedir(&Directory);
    
    if (result == FR_OK)
    {
        // Use just the filename with IMAGES_DIR
        // IMAGES_DIR already includes "SD:"
        snprintf(FullPath, sizeof(FullPath), "%s/%s", IMAGES_DIR, SelectedISO);
    }
    else
    {
        // Use just the filename with DRIVE
        snprintf(FullPath, sizeof(FullPath), "%s/%s", DRIVE, SelectedISO);
    }
    
    LOGNOTE("Loading image from path: %s", FullPath);
    
    // Load the new ISO with full path
    CCueBinFileDevice* CueBinFileDevice = loadCueBinFileDevice(FullPath);
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
