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

// Static global pointer to the kernel instance (needed for the callback)
static CKernel* g_pKernel = nullptr;

LOGMODULE("kernel");

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
		
		// Add button update call
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

void CKernel::ButtonEventHandler(unsigned nButtonIndex, boolean bPressed, void* pParam)
{
    CKernel* pKernel = static_cast<CKernel*>(pParam);
    if (pKernel == nullptr || pKernel->m_pButtonManager == nullptr)
    {
        return;
    }
    
    // Only handle button press events (not releases)
    if (bPressed)
    {
        const char* buttonLabel = pKernel->m_pButtonManager->GetButtonLabel(nButtonIndex);
        
        // Log button press with more detail
        LOGNOTE("Button pressed: %s (index %u)", buttonLabel, nButtonIndex);
        
        // Flash the activity LED briefly to indicate button press
        pKernel->m_ActLED.On();
        
        // Get display type to handle buttons differently
        TDisplayType displayType = pKernel->m_pButtonManager->GetDisplayType();
        
        // Handle button differently based on display type
        if (displayType == DisplayTypeSH1106)
        {
            // Handle buttons based on current screen state
            switch (pKernel->m_ScreenState)
            {
                case ScreenStateMain:
                    // Main screen button handling
                    switch (nButtonIndex)
                    {
                        case 5: // KEY1 button - Load ISO Screen
                            LOGNOTE("SH1106: KEY1 button - Load ISO Screen");
                            pKernel->m_ScreenState = ScreenStateLoadISO;
                            pKernel->ScanForISOFiles();
                            pKernel->ShowISOSelectionScreen();
                            break;
                            
                        case 6: // KEY2 button - Advanced Menu
                            LOGNOTE("SH1106: KEY2 button - Advanced Menu");
                            pKernel->m_ScreenState = ScreenStateAdvanced;
                            // Placeholder for advanced menu
                            if (pKernel->m_pDisplayManager != nullptr)
                            {
                                pKernel->m_pDisplayManager->ShowStatusScreen(
                                    "Advanced Menu",
                                    "Functions to be added",
                                    "Press KEY2 to return");
                            }
                            break;
                    }
                    break;
                    
                case ScreenStateLoadISO:
                    // ISO selection screen button handling
                    switch (nButtonIndex)
                    {
                        case 0: // D-UP button - Scroll up through ISO list
                            LOGNOTE("SH1106: UP button - Previous ISO");
                            if (pKernel->m_nTotalISOCount > 0 && pKernel->m_nCurrentISOIndex > 0)
                            {
                                pKernel->m_nCurrentISOIndex--;
                                pKernel->ShowISOSelectionScreen();
                            }
                            break;
                            
                        case 1: // D-DOWN button - Scroll down through ISO list
                            LOGNOTE("SH1106: DOWN button - Next ISO");
                            if (pKernel->m_nTotalISOCount > 0 && 
                                pKernel->m_nCurrentISOIndex < pKernel->m_nTotalISOCount - 1)
                            {
                                pKernel->m_nCurrentISOIndex++;
                                pKernel->ShowISOSelectionScreen();
                            }
                            break;
                            
                        case 5: // KEY1 button - Select displayed ISO
                            LOGNOTE("SH1106: KEY1 button - Select ISO");
                            pKernel->LoadSelectedISO();
                            pKernel->m_ScreenState = ScreenStateMain;
                            break;
                            
                        case 6: // KEY2 button - Cancel ISO selection
                            LOGNOTE("SH1106: KEY2 button - Cancel selection");
                            pKernel->m_ScreenState = ScreenStateMain;
                            // Return to main screen without changing ISO
                            CPropertiesFatFsFile Properties(CONFIG_FILE, &pKernel->m_FileSystem);
                            Properties.Load();
                            Properties.SelectSection("usbode");
                            const char* currentImage = Properties.GetString("current_image", "image.iso");
                            pKernel->UpdateDisplayStatus(currentImage);
                            break;
                    }
                    break;
                    
                case ScreenStateAdvanced:
                    // Advanced menu button handling
                    switch (nButtonIndex)
                    {
                        case 6: // KEY2 button - Return to main screen
                            LOGNOTE("SH1106: KEY2 button - Return to main");
                            pKernel->m_ScreenState = ScreenStateMain;
                            // Return to main screen
                            CPropertiesFatFsFile Properties(CONFIG_FILE, &pKernel->m_FileSystem);
                            Properties.Load();
                            Properties.SelectSection("usbode");
                            const char* currentImage = Properties.GetString("current_image", "image.iso");
                            pKernel->UpdateDisplayStatus(currentImage);
                            break;
                    }
                    break;
            }
        }
        else if (displayType == DisplayTypeST7789)
        {
            // ST7789 (Pirate Audio) handling - keeping as is
            switch (nButtonIndex)
            {
                case 0: // A button (usually UP)
                    LOGNOTE("ST7789: A button - Previous image");
                    break;
                    
                case 1: // B button (usually DOWN)
                    LOGNOTE("ST7789: B button - Next image");
                    break;
                    
                case 2: // X button (usually BACK/CANCEL)
                    LOGNOTE("ST7789: X button - Cancel/Back");
                    break;
                    
                case 3: // Y button (usually SELECT/CONFIRM)
                    LOGNOTE("ST7789: Y button - Select/Confirm");
                    break;
                    
                default:
                    LOGNOTE("ST7789: Unknown button %u", nButtonIndex);
                    break;
            }
        }
        
        // Turn off the LED after a short delay
        pKernel->m_Scheduler.MsSleep(100);
        pKernel->m_ActLED.Off();
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
    // Clean up previous list if it exists
    if (m_pISOList != nullptr)
    {
        delete[] m_pISOList;
        m_pISOList = nullptr;
    }
    
    // Allocate new list
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
    
    // Get current ISO from config
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");
    
    // Open the root directory
    DIR Directory;
    FILINFO FileInfo;
    
    if (f_opendir(&Directory, DRIVE) != FR_OK)
    {
        LOGERR("Cannot open root directory");
        return;
    }
    
    // Read all files
    while (f_readdir(&Directory, &FileInfo) == FR_OK && FileInfo.fname[0] != 0)
    {
        // Skip directories
        if (FileInfo.fattrib & AM_DIR)
        {
            continue;
        }
        
        // Check for .iso, .cue, or .bin extensions
        const char* Extension = strrchr(FileInfo.fname, '.');
        if (Extension != nullptr && 
            (strcasecmp(Extension, ".iso") == 0 || 
             strcasecmp(Extension, ".cue") == 0 || 
             strcasecmp(Extension, ".bin") == 0))
        {
            // Add to our list if we have space
            if (m_nTotalISOCount < MAX_ISO_FILES)
            {
                m_pISOList[m_nTotalISOCount] = FileInfo.fname;
                
                // Check if this is the current image
                if (strcasecmp(FileInfo.fname, currentImage) == 0)
                {
                    m_nCurrentISOIndex = m_nTotalISOCount;
                }
                
                m_nTotalISOCount++;
            }
            else
            {
                LOGWARN("Maximum ISO file count reached (%u)", MAX_ISO_FILES);
                break;
            }
        }
    }
    
    f_closedir(&Directory);
    
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
    
    // Prepare header text
    char HeaderText[32];
    snprintf(HeaderText, sizeof(HeaderText), "Select ISO (%u/%u)", 
             m_nCurrentISOIndex + 1, m_nTotalISOCount);
    
    if (m_nTotalISOCount == 0)
    {
        // No ISO files found
        m_pDisplayManager->ShowStatusScreen(
            HeaderText,
            "No ISO files found",
            "Place files on SD card");
    }
    else
    {
        // Display current file in the selection
        const char* CurrentFile = (const char*)m_pISOList[m_nCurrentISOIndex];
        
        m_pDisplayManager->ShowFileSelectionScreen(
            "Current: Select to load",
            CurrentFile,
            m_nCurrentISOIndex + 1,
            m_nTotalISOCount);
    }
}

void CKernel::LoadSelectedISO(void)
{
    // Early exit if no files
    if (m_nTotalISOCount == 0 || m_pISOList == nullptr)
    {
        return;
    }
    
    // Get the selected ISO filename
    const char* SelectedISO = (const char*)m_pISOList[m_nCurrentISOIndex];
    
    // Update the config file
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    Properties.SetString("current_image", SelectedISO);
    Properties.Save();
    
    LOGNOTE("Selected new ISO: %s", SelectedISO);
    
    // Show loading message
    if (m_pDisplayManager != nullptr)
    {
        m_pDisplayManager->ShowStatusScreen(
            "Loading new ISO",
            SelectedISO,
            "Please wait...");
    }
    
    // Load the new ISO
    CCueBinFileDevice* CueBinFileDevice = loadCueBinFileDevice(SelectedISO);
    if (CueBinFileDevice == nullptr)
    {
        LOGERR("Failed to load ISO: %s", SelectedISO);
        
        // Show error message
        if (m_pDisplayManager != nullptr)
        {
            m_pDisplayManager->ShowStatusScreen(
                "Error loading ISO",
                SelectedISO,
                "Check file format");
        }
        
        return;
    }
    
    // Set the new device in the CD gadget
    m_CDGadget.SetDevice(CueBinFileDevice);
    
    // Update the display
    UpdateDisplayStatus(SelectedISO);
}
