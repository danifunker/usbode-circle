#include "homepage.h"

#include <circle/logger.h>
#include <gitinfo/gitinfo.h>

#include "../../../src/kernel.h"

LOGMODULE("homepage");

SH1106HomePage::SH1106HomePage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
    LOGNOTE("Homepage starting");
}

SH1106HomePage::~SH1106HomePage() {
}

void SH1106HomePage::OnEnter() {
    LOGNOTE("Drawing homepage");
    pTitle = GetVersionString();
    pUSBSpeed = GetUSBSpeed();
    pISOName = GetCurrentImage();
    pIPAddress = GetIPAddress();
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
    // We shouldn't redraw everything all the time!
    const char* ISOName = GetCurrentImage();
    if (strcmp(ISOName, pISOName) != 0) {
	pISOName = ISOName;
        Draw();
    }

    const char* IPAddress = GetIPAddress();
    if (strcmp(IPAddress, pIPAddress) != 0) {
	pIPAddress = IPAddress;
        Draw();
    }

}

const char* SH1106HomePage::GetIPAddress() {
    CNetSubSystem* net = CKernel::Get()->GetNetwork();
    if (net && net->IsRunning()) {
        CString IPString;
        net->GetConfig()->GetIPAddress()->Format(&IPString);
        return (const char*)IPString;
    } else {
        return "Not Connected";
    }
}

const char* SH1106HomePage::GetVersionString() {
    return CGitInfo::Get()->GetShortVersionString();
}

const char* SH1106HomePage::GetCurrentImage() {
    return m_Service->GetCurrentCDName();
}

const char* SH1106HomePage::GetUSBSpeed() {
    if (config->GetUSBFullSpeed())
        return "FullSpeed";
    return "HighSpeed";
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

    // ISO name (with two-line support)
    size_t first_line_chars = 19;  // Increased from 16 to 19
    size_t second_line_chars = 21; // Increased from 18 to 21

    char first_line[32] = {0};
    char second_line[32] = {0};
    size_t iso_length = strlen(pISOName);

    if (iso_length <= first_line_chars)
    {
	// Short name fits on one line
        m_Graphics->DrawText(12, 27, COLOR2D(255,255,255), pISOName, C2DGraphics::AlignLeft, Font6x7);
    }
    else
    {
	// First line (with CD icon offset)
	strncpy(first_line, pISOName, first_line_chars);
	first_line[first_line_chars] = '\0';
        m_Graphics->DrawText(12, 27, COLOR2D(255,255,255), first_line, C2DGraphics::AlignLeft, Font6x7);

	// Second line (potentially with ellipsis for very long names)
	if (iso_length > first_line_chars + second_line_chars - 4)
	{
	    // Very long name, use ellipsis and last 13 chars (increased from 9)
	    strncpy(second_line, pISOName + first_line_chars, second_line_chars - 17); // Adjust for "..." + 13 chars
	    strcat(second_line, "...");
	    strcat(second_line, pISOName + iso_length - 13); // Show last 13 chars instead of 9
	}
	else
	{
	    strncpy(second_line, pISOName + first_line_chars, second_line_chars);
	    second_line[second_line_chars] = '\0';
	}

        m_Graphics->DrawText(0, 37, COLOR2D(255,255,255), second_line, C2DGraphics::AlignLeft, Font6x7);
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
    m_Graphics->DrawText(10, 49, COLOR2D(255,255,255), pUSBSpeed, C2DGraphics::AlignLeft, Font6x7);

    // Ensure the display is updated with all changes
    m_Graphics->UpdateDisplay();
}

