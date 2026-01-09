#include "homepage.h"

#include <circle/logger.h>
#include <gitinfo/gitinfo.h>
#include <cstring>

#include "../../../src/kernel.h"

LOGMODULE("homepage");

SH1106HomePage::SH1106HomePage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    // Initialize scroll variables
    CCharGenerator Font(Font6x7, CCharGenerator::FontFlagsNone);
    m_ISOCharWidth = Font.GetCharWidth();
    m_ISOMaxTextPx = m_Display->GetWidth() - 12;  // Account for CD icon offset
    pISOPath[0] = '\0';

    LOGNOTE("Homepage starting");
}

SH1106HomePage::~SH1106HomePage() {
}

void SH1106HomePage::OnEnter() {
    LOGNOTE("Drawing homepage");
    pTitle = GetVersionString();
    pUSBSpeed = GetUSBSpeed();

    // Get full path and store it
    const char* path = GetCurrentImagePath();
    strncpy(pISOPath, path, MAX_PATH_LEN - 1);
    pISOPath[MAX_PATH_LEN - 1] = '\0';

    // Reset scroll state
    m_ISOScrollOffsetPx = 0;
    m_ISOScrollDirLeft = true;

    GetIPAddress(pIPAddress, sizeof(pIPAddress));
    Draw();
}

void SH1106HomePage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106HomePage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106HomePage::nextPageName() {
    return m_NextPageName;
}

void SH1106HomePage::OnButtonPress(Button button) {
    LOGNOTE("Button received by page %d", button);

    switch (button) {
        case Button::Up:
            m_NextPageName = "imagespage";
            m_ShouldChangePage = true;
            break;

        case Button::Down:
            m_NextPageName = "imagespage";
            m_ShouldChangePage = true;
            break;

        case Button::Cancel:
            m_NextPageName = "configpage";
            m_ShouldChangePage = true;
            break;

        case Button::Center:
        case Button::Ok:
            m_NextPageName = "imagespage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
}

void SH1106HomePage::Refresh() {
    // Check if ISO path changed
    const char* currentPath = GetCurrentImagePath();
    if (strcmp(currentPath, pISOPath) != 0) {
        strncpy(pISOPath, currentPath, MAX_PATH_LEN - 1);
        pISOPath[MAX_PATH_LEN - 1] = '\0';
        m_ISOScrollOffsetPx = 0;
        m_ISOScrollDirLeft = true;
        Draw();
        return;
    }

    // Check if IP changed
    char IPAddress[16];
    GetIPAddress(IPAddress, sizeof(IPAddress));
    if (strcmp(IPAddress, pIPAddress) != 0) {
        strcpy(pIPAddress, IPAddress);
        Draw();
        return;
    }

    // Scroll the ISO path if needed
    RefreshISOScroll();
}

void SH1106HomePage::GetIPAddress(char* buffer, size_t size) {
    CNetSubSystem* net = CKernel::Get()->GetNetwork();
    if (net && net->IsRunning()) {
        CString IPString;
        net->GetConfig()->GetIPAddress()->Format(&IPString);
        strncpy(buffer, IPString.c_str(), size);
        buffer[size-1] = '\0';
    } else {
        strncpy(buffer, "Not Connected", size);
    }
}

const char* SH1106HomePage::GetVersionString() {
    return CGitInfo::Get()->GetShortVersionString();
}

const char* SH1106HomePage::GetCurrentImagePath() {
    const char* path = m_Service->GetCurrentCDPath();
    if (path == nullptr || path[0] == '\0')
        return "Loading...";

    // Skip "1:/" prefix if present
    if (strncmp(path, "1:/", 3) == 0)
        path += 3;

    return path;
}

const char* SH1106HomePage::GetUSBSpeed() {
    if (config->GetUSBTargetOS() == USBTargetOS::Apple)
        return "Classic Mac (1.1)"; // Classic Mac mode is always FullSpeed
    if (config->GetUSBFullSpeed())
        return "FullSpeed (1.1)";
    return "HighSpeed (2.0)";
}

void SH1106HomePage::Draw() {

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), pTitle, C2DGraphics::AlignLeft, Font6x7);

    // Draw WiFi icon using pixel operations
    unsigned int wifi_x = 0;
    unsigned int wifi_y = 14;

    // WiFi base dot (center)
    m_Graphics->DrawPixel(wifi_x+4, wifi_y+6, COLOR2D(255, 255, 255));
    m_Graphics->DrawPixel(wifi_x+4, wifi_y+5, COLOR2D(255, 255, 255));

    // Inner arc
    for (unsigned int x = wifi_x+2; x <= wifi_x+6; x++) {
	m_Graphics->DrawPixel(x, wifi_y+4, COLOR2D(255, 255, 255));
	m_Graphics->DrawPixel(x, wifi_y+3, COLOR2D(255, 255, 255));
    }

    // Middle arc
    for (unsigned int x = wifi_x+1; x <= wifi_x+7; x++) {
	m_Graphics->DrawPixel(x, wifi_y+2, COLOR2D(255, 255, 255));
    }
    for (unsigned int x = wifi_x; x <= wifi_x+8; x++) {
	m_Graphics->DrawPixel(x, wifi_y+1, COLOR2D(255, 255, 255));
    }

    // Outer arc
    for (unsigned int x = wifi_x; x <= wifi_x+8; x++) {
	m_Graphics->DrawPixel(x, wifi_y, COLOR2D(255, 255, 255));
    }

    // Draw IP address
    m_Graphics->DrawText(10, 14, COLOR2D(255,255,255), pIPAddress, C2DGraphics::AlignLeft, Font6x7);

    // Draw CD icon
    unsigned int cd_x = 0;
    unsigned int cd_y = 27;

    // Draw CD as a ring
    for (int y = -4; y <= 4; y++) {
	for (int x = -4; x <= 4; x++) {
	    int dist_squared = x*x + y*y;
	    // Draw pixels between inner and outer radius
	    if (dist_squared <= 16 && dist_squared > 4) {
		unsigned int px = cd_x+4+x;
		unsigned int py = cd_y+4+y;
		// Ensure coordinates are in valid range
		if (px < CSH1106Display::OLED_WIDTH && py < CSH1106Display::OLED_HEIGHT) {
		    m_Graphics->DrawPixel(px, py, COLOR2D(255, 255, 255));
		}
	    }
	}
    }

    // ISO path display - use single line with marquee scrolling for long paths
    size_t pathLen = strlen(pISOPath);
    int fullTextPx = (int)pathLen * m_ISOCharWidth;

    if (fullTextPx <= m_ISOMaxTextPx) {
        // Short path fits on one line - just display it
        m_Graphics->DrawText(12, 30, COLOR2D(255, 255, 255), pISOPath, C2DGraphics::AlignLeft, Font6x7);
    } else {
        // Long path - will be scrolled by RefreshISOScroll(), just draw static for now
        DrawTextScrolled(12, 30, COLOR2D(255, 255, 255), pISOPath, m_ISOScrollOffsetPx, Font6x7);
    }

    // Draw USB icon - pixel by pixel for better control
    unsigned int usb_x = 0;
    unsigned int usb_y = 49;

    // USB outline - rectangular shape
    for (unsigned int x = usb_x; x <= usb_x+8; x++) {
	// Top and bottom lines
	m_Graphics->DrawPixel(x, usb_y, COLOR2D(255,255,255));
	m_Graphics->DrawPixel(x, usb_y+7, COLOR2D(255,255,255));
    }

    for (unsigned int y = usb_y; y <= usb_y+7; y++) {
	// Left and right sides
	m_Graphics->DrawPixel(usb_x, y, COLOR2D(255,255,255));
	m_Graphics->DrawPixel(usb_x+8, y, COLOR2D(255,255,255));
    }

    // USB pins
    for (unsigned int y = usb_y+2; y <= usb_y+5; y++) {
	// Left pin
	m_Graphics->DrawPixel(usb_x+2, y, COLOR2D(255,255,255));
	m_Graphics->DrawPixel(usb_x+3, y, COLOR2D(255,255,255));

	// Right pin
	m_Graphics->DrawPixel(usb_x+5, y, COLOR2D(255,255,255));
	m_Graphics->DrawPixel(usb_x+6, y, COLOR2D(255,255,255));
    }

    // Draw USB speed info next to the USB icon
    m_Graphics->DrawText(10, 49, COLOR2D(255, 255, 255), pUSBSpeed, C2DGraphics::AlignLeft, Font6x7);

    // Ensure the display is updated with all changes
    m_Graphics->UpdateDisplay();
}

void SH1106HomePage::DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                      int pixelOffset, const TFont& rFont) {
    CCharGenerator Font(rFont, CCharGenerator::FontFlagsNone);

    unsigned m_nWidth = m_Graphics->GetWidth();
    unsigned m_nHeight = m_Graphics->GetHeight();
    unsigned drawX = nX - pixelOffset;

    for (; *pText != '\0'; ++pText) {
        for (unsigned y = 0; y < Font.GetUnderline(); y++) {
            CCharGenerator::TPixelLine Line = Font.GetPixelLine(*pText, y);
            for (unsigned x = 0; x < Font.GetCharWidth(); x++) {
                unsigned finalX = drawX + x;
                if (finalX >= nX && finalX < m_nWidth && (nY + y) < m_nHeight) {
                    if (Font.GetPixel(x, Line)) {
                        m_Graphics->DrawPixel(finalX, nY + y, Color);
                    }
                }
            }
        }

        if (*pText == ' ') {
            drawX += Font.GetCharWidth() / 2;
        } else {
            drawX += Font.GetCharWidth();
        }
    }
}

void SH1106HomePage::RefreshISOScroll() {
    size_t pathLen = strlen(pISOPath);
    int fullTextPx = (int)pathLen * m_ISOCharWidth;

    if (fullTextPx <= m_ISOMaxTextPx) {
        // No scrolling needed
        return;
    }

    // Update scroll position
    if (m_ISOScrollDirLeft) {
        m_ISOScrollOffsetPx += 2;
        if (m_ISOScrollOffsetPx >= (fullTextPx - m_ISOMaxTextPx)) {
            m_ISOScrollOffsetPx = (fullTextPx - m_ISOMaxTextPx);
            m_ISOScrollDirLeft = false;
        }
    } else {
        m_ISOScrollOffsetPx -= 2;
        if (m_ISOScrollOffsetPx <= 0) {
            m_ISOScrollOffsetPx = 0;
            m_ISOScrollDirLeft = true;
        }
    }

    // Clear the ISO display area and redraw
    m_Graphics->DrawRect(12, 27, m_Display->GetWidth() - 12, 18, COLOR2D(0, 0, 0));
    DrawTextScrolled(12, 30, COLOR2D(255, 255, 255), pISOPath, m_ISOScrollOffsetPx, Font6x7);
    m_Graphics->UpdateDisplay();
}

