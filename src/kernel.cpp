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

#include "gitinfo.d"
#include "util.h"
#include "webserver.h"

#define DRIVE "SD:"
#define FIRMWARE_PATH DRIVE "/firmware/"
#define SUPPLICANT_CONFIG_FILE DRIVE "/wpa_supplicant.conf"
#define CONFIG_FILE DRIVE "/config.txt"
#define LOG_FILE DRIVE "/logfile.txt"
#define HOSTNAME "usbode"
#define SPI_MASTER_DEVICE 0

// Define the images directory
#define IMAGES_DIR "SD:/images"

// Static global pointer to the kernel instance (needed for the callback)
static CKernel* g_pKernel = nullptr;

LOGMODULE("kernel");

// Custom implementation of strrchr since it's not available in Circle
static const char* FindLastOccurrence(const char* str, int ch) {
    if (str == nullptr)
        return nullptr;

    const char* last = nullptr;

    while (*str != '\0') {
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
      m_pISOList(nullptr) {
    // m_ActLED.Blink(5);  // show we are alive
}

CKernel::~CKernel(void) {
    // Clean up ISO list if allocated
    if (m_pISOList != nullptr) {
        delete[] m_pISOList;
        m_pISOList = nullptr;
    }

    if (m_pButtonManager != nullptr) {
        delete m_pButtonManager;
        m_pButtonManager = nullptr;
    }

    if (m_pDisplayManager != nullptr) {
        delete m_pDisplayManager;
        m_pDisplayManager = nullptr;
    }

    if (m_pSPIMaster != nullptr) {
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
    }
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

    // If network initialization succeeded, read timezone from config.txt
    // Commented because Network definately won't be running yet and
    // we start this later in the run loop anyway
    /*
    if (bOK && m_Net.IsRunning()) {
        // Read timezone from config.txt
        CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
        if (Properties.Load()) {
            Properties.SelectSection("usbode");
            CString Value;
            const char* timezone = Properties.GetString(ConfigOptionTimeZone, "UTC");

            // Initialize NTP with the timezone
            InitializeNTP(timezone);
        } else {
            // Use default timezone if config file not available
            InitializeNTP("UTC");
        }
    }
    */

    return bOK;
}

TShutdownMode CKernel::Run(void) {
    // Initialize the global kernel pointer
    g_pKernel = this;

    // Load our config file loader
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    if (!Properties.Load()) {
        LOGERR("Error loading properties from %s (line %u)",
               CONFIG_FILE, Properties.GetErrorLine());
        return ShutdownHalt;
    }
    Properties.SelectSection("usbode");

    // Start the file logging service
    const char* logfile = Properties.GetString("logfile", nullptr);
    if (logfile) {
        new CFileLogDaemon(logfile);
        LOGNOTE("Started the Log File service");
    }

    LOGNOTE("=====================================");
    LOGNOTE("Welcome to USBODE");
    LOGNOTE("Compile time: " __DATE__ " " __TIME__);
    LOGNOTE("Git Info: %s @ %s", GIT_BRANCH, GIT_COMMIT);
    LOGNOTE("=====================================");

    // Initialize the CD Player service
    const char* pSoundDevice = m_Options.GetSoundDevice();
    if (strcmp(pSoundDevice, "sndi2s") == 0) {
        new CCDPlayer(pSoundDevice);
        LOGNOTE("Started the CD Player service");
    }

    // Load our current disc image
    const char* imageName = Properties.GetString("current_image", "image.iso");
    CCueBinFileDevice* cueBinFileDevice = loadCueBinFileDevice(imageName);
    if (!cueBinFileDevice) {
        LOGERR("Failed to load cueBinFileDevice %s", imageName);
        return ShutdownHalt;
    }
    LOGNOTE("Loaded cue/bin file %s", imageName);

    // Initialize USB CD gadget
    m_CDGadget.SetDevice(cueBinFileDevice);
    if (!m_CDGadget.Initialize()) {
        LOGERR("Failed to initialize USB CD gadget");
        return ShutdownHalt;
    }
    LOGNOTE("Started USB CD gadget");

    // Display configuration
    const char* displayType = Properties.GetString("displayhat", "none");

    // Initialize the appropriate display type
    TDisplayType displayTypeEnum = GetDisplayTypeFromString(displayType);
    if (displayTypeEnum != DisplayTypeUnknown) {
        // Initialize display based on type
        InitializeDisplay(displayTypeEnum);

        // If display was initialized successfully
        if (m_pDisplayManager != nullptr) {
            // Show status screen with current information
            CString IPString;
            if (m_Net.IsRunning()) {
                m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
            } else {
                IPString = "Not connected";
            }

            m_pDisplayManager->ShowStatusScreen(
                "USBODE v2.00-pre1",
                (const char*)IPString,
                imageName,
                m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");  // Add USB speed parameter
        }

        // Allow some time for USB to stabilize before initializing buttons
        LOGNOTE("Waiting for USB to stabilize before initializing buttons");
        m_Scheduler.MsSleep(2000);

        // Initialize buttons AFTER USB CD and display are initialized
        InitializeButtons(displayTypeEnum);
        LOGNOTE("Configured Display hat: %s", displayType);
    }

    static const char ServiceName[] = HOSTNAME;
    static const char* ppText[] = {"path=/index.html", nullptr};
    CmDNSPublisher* pmDNSPublisher = nullptr;
    CWebServer* pCWebServer = nullptr;
    CFTPDaemon* m_pFTPDaemon = nullptr;

    // Previous IP tracking
    static CString PreviousIPString = "";
    bool ntpInitialized = false;

    // Status update timing
    static unsigned lastStatusUpdate = 0;
    const unsigned STATUS_UPDATE_INTERVAL = 30000;  // Update every 30 seconds

    // Main Loop
    for (unsigned nCount = 0; 1; nCount++) {
        // Process button updates FIRST for best responsiveness
        if (m_pButtonManager != nullptr) {
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
        if (m_Net.IsRunning()) {
            m_Net.Process();
        }

        // Start the Web Server
        if (m_Net.IsRunning() && pCWebServer == nullptr) {
            // Create the web server
            pCWebServer = new CWebServer(&m_Net, &m_CDGadget, &m_ActLED, &Properties);

            LOGNOTE("Started Webserver service");
        }

        // Show details of the network connection
        if (m_Net.IsRunning()) {
            CString CurrentIPString;
            m_Net.GetConfig()->GetIPAddress()->Format(&CurrentIPString);

            // If IP changed (including from not connected to connected)
            if (strcmp((const char*)CurrentIPString, (const char*)PreviousIPString) != 0) {
                // Log the new IP address
                LOGNOTE("IP address: %s", (const char*)CurrentIPString);

                // Store for next time - make sure to use deep copy
                PreviousIPString = CurrentIPString;

                // If network is newly up and running with a valid IP address
                // and NTP hasn't been initialized yet, do it now
                if (!ntpInitialized && CurrentIPString != "0.0.0.0") {
                    // Read timezone from config.txt
                    Properties.SelectSection("usbode");
                    const char* timezone = Properties.GetString(ConfigOptionTimeZone, "UTC");

                    // Initialize NTP with the timezone
                    InitializeNTP(timezone);
                    ntpInitialized = true;
                }

                // Update the display with the new IP address
                // but only if we're not in the ISO selection screen
                if (m_ScreenState != ScreenStateLoadISO && m_pDisplayManager != nullptr) {
                    // Get the current ISO name
                    Properties.SelectSection("usbode");
                    const char* currentImage = Properties.GetString("current_image", "image.iso");

                    // Force an update of the display
                    UpdateDisplayStatus(currentImage);
                }
            }
        }

        // Publish mDNS
        if (m_Net.IsRunning() && pmDNSPublisher == nullptr) {
            pmDNSPublisher = new CmDNSPublisher(&m_Net);
            if (!pmDNSPublisher->PublishService(ServiceName, "_http._tcp", 80, ppText)) {
                LOGNOTE("Cannot publish service");
            }
            LOGNOTE("Started mDNS service");
        }

        // Start the FTP Server
        if (m_Net.IsRunning() && !m_pFTPDaemon) {
            m_pFTPDaemon = new CFTPDaemon("cdrom", "cdrom");
            if (!m_pFTPDaemon->Initialize()) {
                LOGERR("Failed to init FTP daemon");
                delete m_pFTPDaemon;
                m_pFTPDaemon = nullptr;
            } else
                LOGNOTE("Started FTP service");
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
        if (m_ScreenState != ScreenStateLoadISO || nCount % 10 == 0) {
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

        // Process display updates if needed - after network processing but before yielding
        if (CWebServer::IsDisplayUpdateNeeded() && m_pDisplayManager != nullptr && m_ScreenState != ScreenStateLoadISO) {
            const char* imageName = CWebServer::GetLastMountedImage();
            LOGNOTE("Processing pending display update for: %s", imageName);

            // Make sure we're not in ISO selection mode
            m_ScreenState = ScreenStateMain;

            // Update the display with the image name
            UpdateDisplayStatus(imageName);

            // Clear the flag
            CWebServer::ClearDisplayUpdateFlag();
        }

	// Give tasks a chance to run
	m_Scheduler.Yield();
    }

    LOGNOTE("ShutdownHalt");
    return ShutdownHalt;
}

TDisplayType CKernel::GetDisplayTypeFromString(const char* displayType) {
    if (displayType == nullptr || strcmp(displayType, "none") == 0) {
        return DisplayTypeUnknown;
    } else if (strcmp(displayType, "pirateaudiolineout") == 0) {
        return DisplayTypeST7789;
    } else if (strcmp(displayType, "waveshare") == 0) {
        return DisplayTypeSH1106;
    }

    // Default to unknown
    return DisplayTypeUnknown;
}

void CKernel::InitializeDisplay(TDisplayType displayType) {
    // Early exit if no display configured
    if (displayType == DisplayTypeUnknown) {
        LOGNOTE("No display configured");
        return;
    }

    // Initialize the appropriate SPI settings based on display type
    if (displayType == DisplayTypeSH1106) {
        LOGNOTE("Initializing SPI for SH1106 display");
        m_pSPIMaster = new CSPIMaster(
            CSH1106Display::SPI_CLOCK_SPEED,
            CSH1106Display::SPI_CPOL,
            CSH1106Display::SPI_CPHA,
            SPI_MASTER_DEVICE);
    } else if (displayType == DisplayTypeST7789) {
        LOGNOTE("Initializing SPI for ST7789 display");
        // Use ST7789-specific SPI settings when they're defined
        m_pSPIMaster = new CSPIMaster(
            80000000,
            0,
            0,
            SPI_MASTER_DEVICE);
    }

    // Initialize the SPI master
    if (m_pSPIMaster == nullptr || !m_pSPIMaster->Initialize()) {
        LOGERR("Failed to initialize SPI master for display");
        if (m_pSPIMaster != nullptr) {
            delete m_pSPIMaster;
            m_pSPIMaster = nullptr;
        }
        return;
    }

    // Create and initialize the display manager
    m_pDisplayManager = new CDisplayManager(&m_Logger, displayType);
    if (m_pDisplayManager == nullptr) {
        LOGERR("Failed to create display manager");
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
        return;
    }

    // Initialize the display
    if (!m_pDisplayManager->Initialize(m_pSPIMaster)) {
        LOGERR("Failed to initialize display");
        delete m_pDisplayManager;
        m_pDisplayManager = nullptr;
        delete m_pSPIMaster;
        m_pSPIMaster = nullptr;
        return;
    }

    LOGNOTE("Display initialized successfully");
}

void CKernel::UpdateDisplayStatus(const char* imageName) {
    // Only update if display manager is initialized
    if (m_pDisplayManager == nullptr) {
        return;
    }

    // CRITICAL: Skip updates completely while in ISO selection screen
    if (m_ScreenState == ScreenStateLoadISO) {
        return;
    }

    // Track the last displayed image and IP to prevent redundant updates
    static CString LastDisplayedIP = "";
    static CString LastDisplayedImage = "";
    static unsigned LastUpdateTime = 0;

    // Debounce display updates - prevent multiple updates within a short time window
    unsigned currentTime = m_Timer.GetTicks();
    if (currentTime - LastUpdateTime < 500)  // 500ms debounce time
    {
        // But don't apply debounce if explicitly requested from web server
        if (imageName == nullptr || *imageName == '\0') {
            return;
        }
    }

    // Always load the current image from properties to ensure consistency
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");

    // Use the provided imageName if it's not null, otherwise use the one from properties
    if (imageName != nullptr && *imageName != '\0') {
        currentImage = imageName;
    }

    // Get current IP address
    CString IPString;
    if (m_Net.IsRunning()) {
        m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
    } else {
        IPString = "Not connected";
    }

    // Force update if explicitly requested by passing a non-null imageName
    bool forceUpdate = (imageName != nullptr && *imageName != '\0');

    // Only update if something has changed or force update requested
    if (forceUpdate || IPString != LastDisplayedIP || CString(currentImage) != LastDisplayedImage) {
        // Get USB speed information for display
        boolean bUSBFullSpeed = m_Options.GetUSBFullSpeed();
        const char* pUSBSpeed = bUSBFullSpeed ? "USB1.1" : "USB2.0";

        // Update the status screen
        m_pDisplayManager->ShowStatusScreen(
            "USBODE v2.00-pre1",
            (const char*)IPString,
            currentImage,
            pUSBSpeed);  // Pass USB speed to display manager

        // Only log when the display actually changes
        LOGNOTE("Display updated: IP=%s, Image=%s, USB=%s",
                (const char*)IPString, currentImage, pUSBSpeed);

        // Store current values
        LastDisplayedIP = IPString;
        LastDisplayedImage = currentImage;
        LastUpdateTime = currentTime;
    }
}

void CKernel::ButtonEventHandler(unsigned nButtonIndex, boolean bPressed, void* pParam) {
    CKernel* pKernel = static_cast<CKernel*>(pParam);
    if (pKernel == nullptr || pKernel->m_pButtonManager == nullptr || pKernel->m_pDisplayManager == nullptr) {
        return;
    }

    // Get display type to handle buttons differently
    TDisplayType displayType = pKernel->m_pDisplayManager->GetDisplayType();

    // Only handle button presses (not releases) for responsiveness
    if (bPressed) {
        // Handle differently based on display type
        if (displayType == DisplayTypeSH1106) {
            // === WAVESHARE SH1106 - Use previous button mapping implementation ===
            // Handle specific actions based on current screen state
            switch (pKernel->m_ScreenState) {
                case ScreenStateMain:
                    // On main screen, only KEY1 (button 5) should work to open ISO selection
                    if (nButtonIndex == 5) {  // KEY1 button
                        // Show a loading message before scanning for files
                        if (pKernel->m_pDisplayManager != nullptr) {
                            pKernel->m_pDisplayManager->ShowStatusScreen(
                                "Please Wait",
                                "Opening Image Browser",
                                "Scanning files...",
                                pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");
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

                    if (nButtonIndex == 0) {  // UP button - previous ISO (single step) with wrapping
                        if (pKernel->m_nTotalISOCount > 0) {
                            // Handle wrapping from first to last file
                            if (pKernel->m_nCurrentISOIndex == 0) {
                                pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 1;
                            } else {
                                pKernel->m_nCurrentISOIndex--;
                            }
                            pKernel->ShowISOSelectionScreen();
                        }
                    } else if (nButtonIndex == 1) {  // DOWN button - next ISO (single step) with wrapping
                        if (pKernel->m_nTotalISOCount > 0) {
                            // Handle wrapping from last to first file
                            if (pKernel->m_nCurrentISOIndex >= pKernel->m_nTotalISOCount - 1) {
                                pKernel->m_nCurrentISOIndex = 0;
                            } else {
                                pKernel->m_nCurrentISOIndex++;
                            }
                            pKernel->ShowISOSelectionScreen();
                        }
                    } else if (nButtonIndex == 2) {  // LEFT button - skip back 5 files with wrapping
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
                    } else if (nButtonIndex == 3) {  // RIGHT button - skip forward 5 files with wrapping
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
                    } else if (nButtonIndex == 5) {  // KEY1 button - load selected ISO
                        // Show loading message
                        if (pKernel->m_pDisplayManager != nullptr) {
                            const char* selectedFile =
                                (pKernel->m_nTotalISOCount > 0 && pKernel->m_pISOList != nullptr) ? (const char*)pKernel->m_pISOList[pKernel->m_nCurrentISOIndex] : "Unknown";

                            pKernel->m_pDisplayManager->ShowStatusScreen(
                                "Please Wait",
                                "Loading Image:",
                                selectedFile,
                                pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");
                            pKernel->m_pDisplayManager->Refresh();
                        }

                        pKernel->LoadSelectedISO();
                        pKernel->m_ScreenState = ScreenStateMain;
                        // Update main screen after loading ISO
                        pKernel->UpdateDisplayStatus(nullptr);
                    } else if (nButtonIndex == 6) {  // KEY2 button - cancel and return to main
                        pKernel->m_ScreenState = ScreenStateMain;

                        // Get the current image that's still loaded to properly refresh the main screen
                        CPropertiesFatFsFile Properties(CONFIG_FILE, &pKernel->m_FileSystem);
                        Properties.Load();
                        Properties.SelectSection("usbode");
                        const char* currentImage = Properties.GetString("current_image", "image.iso");

                        // Force a display update to ensure the main screen refreshes
                        pKernel->UpdateDisplayStatus(currentImage);
                    }
                    // Add handling for center joystick button (usually button 4 or 8) to select an ISO:
                    else if (nButtonIndex == 4 || nButtonIndex == 8) {  // JOYSTICK_PRESS button - load selected ISO
                        // Show loading message
                        if (pKernel->m_pDisplayManager != nullptr) {
                            const char* selectedFile =
                                (pKernel->m_nTotalISOCount > 0 && pKernel->m_pISOList != nullptr) ? (const char*)pKernel->m_pISOList[pKernel->m_nCurrentISOIndex] : "Unknown";

                            pKernel->m_pDisplayManager->ShowStatusScreen(
                                "Please Wait",
                                "Loading Image:",
                                selectedFile,
                                pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");
                            pKernel->m_pDisplayManager->Refresh();
                        }

                        pKernel->LoadSelectedISO();
                        pKernel->m_ScreenState = ScreenStateMain;
                        // Update main screen after loading ISO
                        pKernel->UpdateDisplayStatus(nullptr);
                    }
                    break;

                default:
                    break;
            }
        } else if (displayType == DisplayTypeST7789) {
            // === PIRATEAUDIO ST7789 (4 buttons) ===
            // Maintain existing button mapping and functionality

            // Get button label for display feedback
            const char* buttonLabel = "";
            switch (nButtonIndex) {
                case 0:
                    buttonLabel = "A (Up)";
                    break;
                case 1:
                    buttonLabel = "B (Down)";
                    break;
                case 2:
                    buttonLabel = "X (Cancel/Menu)";
                    break;
                case 3:
                    buttonLabel = "Y (Select)";
                    break;
                default:
                    buttonLabel = "Unknown";
                    break;
            }

            // Show button press on display
            pKernel->m_pDisplayManager->ShowButtonPress(nButtonIndex, buttonLabel);

            // Handle specific actions based on current screen state
            switch (pKernel->m_ScreenState) {
                case ScreenStateMain:
                    // Main screen button handling
                    if (nButtonIndex == 0 || nButtonIndex == 1 || nButtonIndex == 3) {
                        // Button A (Up), B (Down) or Y (Select) - Open ISO selection
                        LOGNOTE("Button %s pressed - Opening ISO selection", buttonLabel);

                        // Show a loading message before scanning for files
                        pKernel->m_pDisplayManager->ShowStatusScreen(
                            "Please Wait",
                            "Scanning for ISOs...",
                            "This may take a moment",
                            pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");

                        // Immediate response to button in main screen
                        pKernel->m_ScreenState = ScreenStateLoadISO;
                        pKernel->ScanForISOFiles();
                        pKernel->ShowISOSelectionScreen();
                    } else if (nButtonIndex == 2) {
                        // Button X - Advanced menu (not implemented yet)
                        LOGNOTE("Button X pressed - Advanced menu (not implemented)");

                        pKernel->m_pDisplayManager->ShowStatusScreen(
                            "Advanced Menu",
                            "Not implemented yet",
                            "Coming soon",
                            pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");
                    }
                    break;

                case ScreenStateLoadISO:
                    // ISO selection screen button handling
                    if (nButtonIndex == 0) {
                        // Button A (Up) - previous ISO with wrapping
                        if (pKernel->m_nTotalISOCount > 0) {
                            if (pKernel->m_nCurrentISOIndex == 0) {
                                // Wrap to the end
                                pKernel->m_nCurrentISOIndex = pKernel->m_nTotalISOCount - 1;
                            } else {
                                pKernel->m_nCurrentISOIndex--;
                            }
                            pKernel->ShowISOSelectionScreen();
                        }
                    } else if (nButtonIndex == 1) {
                        // Button B (Down) - next ISO with wrapping
                        if (pKernel->m_nTotalISOCount > 0) {
                            pKernel->m_nCurrentISOIndex = (pKernel->m_nCurrentISOIndex + 1) % pKernel->m_nTotalISOCount;
                            pKernel->ShowISOSelectionScreen();
                        }
                    } else if (nButtonIndex == 2) {
                        // Button X (Cancel) - return to main screen
                        pKernel->m_ScreenState = ScreenStateMain;

                        // Get the current image that's still loaded
                        CPropertiesFatFsFile Properties(CONFIG_FILE, &pKernel->m_FileSystem);
                        Properties.Load();
                        Properties.SelectSection("usbode");
                        const char* currentImage = Properties.GetString("current_image", "image.iso");

                        // Update main screen
                        pKernel->UpdateDisplayStatus(currentImage);
                    } else if (nButtonIndex == 3) {
                        // Button Y (Select) - load selected ISO
                        // Show loading message
                        const char* selectedFile =
                            (pKernel->m_nTotalISOCount > 0 && pKernel->m_pISOList != nullptr) ? (const char*)pKernel->m_pISOList[pKernel->m_nCurrentISOIndex] : "Unknown";

                        pKernel->m_pDisplayManager->ShowStatusScreen(
                            "Please Wait",
                            "Loading Image:",
                            selectedFile,
                            pKernel->m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");
                        pKernel->m_pDisplayManager->Refresh();

                        pKernel->LoadSelectedISO();
                        pKernel->m_ScreenState = ScreenStateMain;
                        // Update main screen after loading ISO
                        pKernel->UpdateDisplayStatus(nullptr);
                    }
                    break;

                default:
                    break;
            }
        } else {
            // Unknown display type - use PirateAudio (ST7789) default mapping
            // This ensures backward compatibility
            LOGWARN("Unknown display type, using default button mapping");

            // Default to ST7789 handling code if needed
        }
    }
}

void CKernel::InitializeButtons(TDisplayType displayType) {
    // Early exit if no display type is specified
    if (displayType == DisplayTypeUnknown) {
        LOGNOTE("No display configured, skipping button initialization");
        return;
    }

    // Skip if buttons are already initialized
    if (m_pButtonManager != nullptr) {
        LOGNOTE("Buttons already initialized");
        return;
    }

    LOGNOTE("Starting button initialization for display type: %s",
            displayType == DisplayTypeSH1106 ? "SH1106" : displayType == DisplayTypeST7789 ? "ST7789"
                                                                                           : "Unknown");

    // Create the button manager
    m_pButtonManager = new CGPIOButtonManager(&m_Logger, displayType);
    if (m_pButtonManager == nullptr) {
        LOGERR("Failed to create button manager");
        return;
    }

    // Initialize the button manager
    if (!m_pButtonManager->Initialize()) {
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

void CKernel::ScanForISOFiles(void) {
    // Show loading indicator immediately before starting the operation
    if (m_pDisplayManager != nullptr) {
        m_pDisplayManager->ShowStatusScreen(
            "Please Wait",
            "Scanning for ISOs...",
            "This may take a moment",
            m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");  // Add USB speed parameter

        // Ensure the display is updated immediately
        m_pDisplayManager->Refresh();
    }

    // Clean up previous list
    if (m_pISOList != nullptr) {
        delete[] m_pISOList;
        m_pISOList = nullptr;
    }

    // Reset counters
    m_nTotalISOCount = 0;
    m_nCurrentISOIndex = 0;

    // Allocate with larger MAX_ISO_FILES value
    m_pISOList = new CString[MAX_ISO_FILES];
    if (m_pISOList == nullptr) {
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
    if (result == FR_OK) {
        LOGNOTE("Scanning for ISO files in %s", IMAGES_DIR);

        // Read all files in this directory
        while (f_readdir(&Directory, &FileInfo) == FR_OK && FileInfo.fname[0] != 0) {
            // Skip directories
            if (FileInfo.fattrib & AM_DIR)
                continue;

            // Check for .iso, .cue, or .bin extensions
            const char* Extension = FindLastOccurrence(FileInfo.fname, '.');
            if (Extension == nullptr)
                continue;

            if (strcasecmp(Extension, ".iso") == 0 ||
                strcasecmp(Extension, ".cue") == 0 ||
                strcasecmp(Extension, ".bin") == 0) {
                // Check if we have space left
                if (m_nTotalISOCount >= MAX_ISO_FILES) {
                    LOGWARN("Maximum ISO file count reached (%u)", MAX_ISO_FILES);
                    break;
                }

                // Add to our list
                m_pISOList[m_nTotalISOCount] = FileInfo.fname;

                // Check if this is the current image
                if (strcasecmp(FileInfo.fname, currentImage) == 0) {
                    m_nCurrentISOIndex = m_nTotalISOCount;
                    currentImageFound = true;
                }

                m_nTotalISOCount++;
            }
        }

        f_closedir(&Directory);
    }

    // Sort files alphabetically using a more efficient algorithm
    for (unsigned i = 0; i < m_nTotalISOCount - 1; i++) {
        unsigned minIndex = i;

        for (unsigned j = i + 1; j < m_nTotalISOCount; j++) {
            if (strcasecmp((const char*)m_pISOList[j], (const char*)m_pISOList[minIndex]) < 0) {
                minIndex = j;
            }
        }

        if (minIndex != i) {
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

void CKernel::ShowISOSelectionScreen(void) {
    if (m_pDisplayManager == nullptr) {
        return;
    }

    // Get current ISO from config
    CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
    Properties.Load();
    Properties.SelectSection("usbode");
    const char* currentImage = Properties.GetString("current_image", "image.iso");

    if (m_nTotalISOCount == 0) {
        // No ISO files found
        m_pDisplayManager->ShowStatusScreen(
            "Select Image",
            "No Images files found",
            "Place files on SD card",
            m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");  // Add USB speed parameter
    } else {
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

void CKernel::LoadSelectedISO(void) {
    // Early validation checks...
    if (m_nTotalISOCount == 0 || m_pISOList == nullptr) {
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
    if (CueBinFileDevice == nullptr) {
        LOGERR("Failed to load Image: %s", SelectedISO);

        // Get the current image that's still loaded
        CPropertiesFatFsFile Properties(CONFIG_FILE, &m_FileSystem);
        Properties.Load();
        Properties.SelectSection("usbode");
        const char* currentImage = Properties.GetString("current_image", "image.iso");

        // Show error message with current loaded image
        if (m_pDisplayManager != nullptr) {
            m_pDisplayManager->ShowStatusScreen(
                "Error loading Image",
                "Failed to load file",
                currentImage,  // Show currently loaded ISO
                m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");

            // Ensure the display is refreshed immediately
            m_pDisplayManager->Refresh();
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

    // Return to main screen state first
    m_ScreenState = ScreenStateMain;

    // Show a success message before returning to main screen
    if (m_pDisplayManager != nullptr) {
        m_pDisplayManager->ShowStatusScreen(
            "Image Loaded",
            "Successfully mounted:",
            SelectedISO,
            m_Options.GetUSBFullSpeed() ? "USB1.1" : "USB2.0");

        // Ensure the display is refreshed immediately
        m_pDisplayManager->Refresh();

        // Small delay to show the success message
        m_Timer.MsDelay(1000);
    }

    // Update the display with the new ISO
    UpdateDisplayStatus(SelectedISO);

    // Explicitly refresh again to ensure display is updated
    if (m_pDisplayManager != nullptr) {
        m_pDisplayManager->Refresh();
    }
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
