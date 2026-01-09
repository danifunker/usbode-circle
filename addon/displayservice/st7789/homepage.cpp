#include "homepage.h"

#include <circle/logger.h>
#include <gitinfo/gitinfo.h>
#include <cstring>

#include "../../../src/kernel.h"

LOGMODULE("homepage");

ST7789HomePage::ST7789HomePage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));

    pISOPath[0] = '\0';
    pISOPathDisplay[0] = '\0';
}

ST7789HomePage::~ST7789HomePage() {
}

void ST7789HomePage::OnEnter() {
    LOGNOTE("Drawing homepage");
    pTitle = GetVersionString();
    pUSBSpeed = GetUSBSpeed();

    // Get full path and store it
    const char* path = GetCurrentImagePath();
    strncpy(pISOPath, path, MAX_PATH_LEN - 1);
    pISOPath[MAX_PATH_LEN - 1] = '\0';

    // Create truncated display version (max ~75 chars for 3 lines at 25 chars each)
    TruncatePathWithEllipsis(pISOPath, pISOPathDisplay, sizeof(pISOPathDisplay), 75);

    GetIPAddress(pIPAddress, sizeof(pIPAddress));
    Draw();
}

void ST7789HomePage::OnExit() {
    m_ShouldChangePage = false;
}

bool ST7789HomePage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789HomePage::nextPageName() {
    return m_NextPageName;
}

void ST7789HomePage::OnButtonPress(Button button) {
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

        case Button::Ok:
            m_NextPageName = "infopage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
}

void ST7789HomePage::Refresh() {
    // Check if ISO path changed
    const char* currentPath = GetCurrentImagePath();
    if (strcmp(currentPath, pISOPath) != 0) {
        strncpy(pISOPath, currentPath, MAX_PATH_LEN - 1);
        pISOPath[MAX_PATH_LEN - 1] = '\0';
        TruncatePathWithEllipsis(pISOPath, pISOPathDisplay, sizeof(pISOPathDisplay), 75);
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
}

void ST7789HomePage::GetIPAddress(char* buffer, size_t size) {
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

const char* ST7789HomePage::GetVersionString() {
    return CGitInfo::Get()->GetShortVersionString();
}

const char* ST7789HomePage::GetCurrentImagePath() {
    const char* path = m_Service->GetCurrentCDPath();
    if (path == nullptr || path[0] == '\0')
        return "Loading...";

    // Skip "1:/" prefix if present
    if (strncmp(path, "1:/", 3) == 0)
        path += 3;

    return path;
}

const char* ST7789HomePage::GetUSBSpeed() {
    if (config->GetUSBTargetOS() == USBTargetOS::Apple)
        return "Classic Mac (1.1)"; // Classic Mac mode is always FullSpeed
    if (config->GetUSBFullSpeed())
        return "FullSpeed (1.1)";
    return "HighSpeed (2.0)";
}

void ST7789HomePage::Draw() {

    // Clear the screen with WHITE background using the graphics object
    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    // Draw header bar with blue background
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));

    // Draw title text in white
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    // Draw WiFi icon as signal bars (3 bars) instead of antenna
    unsigned wifi_x = 10;
    unsigned wifi_y = 40;

    // Base of WiFi icon
    m_Graphics->DrawRect(wifi_x + 8, wifi_y + 16, 4, 4, COLOR2D(0, 0, 0));

    // First (shortest) bar
    m_Graphics->DrawRect(wifi_x + 7, wifi_y + 11, 6, 3, COLOR2D(0, 0, 0));

    // Second (medium) bar
    m_Graphics->DrawRect(wifi_x + 4, wifi_y + 6, 12, 3, COLOR2D(0, 0, 0));

    // Third (longest) bar
    m_Graphics->DrawRect(wifi_x + 1, wifi_y + 1, 18, 3, COLOR2D(0, 0, 0));

    // Draw IP address
    m_Graphics->DrawText(35, 45, COLOR2D(0, 0, 0), pIPAddress, C2DGraphics::AlignLeft);

    // Move the USB icon further down to accommodate 3 lines of text and make it larger
    unsigned usb_x = 10;
    unsigned usb_y = 75;  // Moved down close to nav bar

    // USB outline - rectangular shape (3x larger)
    // Using DrawRect for thicker lines instead of individual pixels
    m_Graphics->DrawRect(usb_x, usb_y, 24, 2, COLOR2D(0, 0, 0));       // Top horizontal
    m_Graphics->DrawRect(usb_x, usb_y + 21, 24, 2, COLOR2D(0, 0, 0));  // Bottom horizontal
    m_Graphics->DrawRect(usb_x, usb_y, 2, 23, COLOR2D(0, 0, 0));       // Left vertical
    m_Graphics->DrawRect(usb_x + 22, usb_y, 2, 23, COLOR2D(0, 0, 0));  // Right vertical

    // USB pins (larger)
    m_Graphics->DrawRect(usb_x + 6, usb_y + 6, 4, 12, COLOR2D(0, 0, 0));   // Left pin
    m_Graphics->DrawRect(usb_x + 14, usb_y + 6, 4, 12, COLOR2D(0, 0, 0));  // Right pin

    // Draw USB speed info next to the USB icon
    m_Graphics->DrawText(40, 80, COLOR2D(0, 0, 0), pUSBSpeed, C2DGraphics::AlignLeft);

    // ALWAYS draw CD icon regardless of ISO status
    unsigned cd_x = 10;
    unsigned cd_y = 115;
    unsigned cd_radius = 10;

    // Draw outer circle of CD
    m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(0, 0, 0));

    // Draw middle circle of CD
    m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(0, 0, 0));

    // Draw center hole of CD
    m_Graphics->DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(0, 0, 0));

    // ISO path display with THREE-line support using pre-truncated path
    size_t first_line_chars = 25;
    size_t second_line_chars = 25;
    size_t third_line_chars = 25;

    char first_line[40] = {0};
    char second_line[40] = {0};
    char third_line[40] = {0};
    size_t path_length = strlen(pISOPathDisplay);

    if (path_length <= first_line_chars) {
        // Short path fits on one line
        m_Graphics->DrawText(35, cd_y, COLOR2D(0, 0, 0), pISOPathDisplay, C2DGraphics::AlignLeft);
    } else if (path_length <= first_line_chars + second_line_chars) {
        // Two lines needed
        strncpy(first_line, pISOPathDisplay, first_line_chars);
        first_line[first_line_chars] = '\0';
        m_Graphics->DrawText(35, cd_y, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);

        strncpy(second_line, pISOPathDisplay + first_line_chars, second_line_chars);
        second_line[second_line_chars] = '\0';
        m_Graphics->DrawText(35, cd_y + 20, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);
    } else {
        // Three lines needed
        strncpy(first_line, pISOPathDisplay, first_line_chars);
        first_line[first_line_chars] = '\0';
        m_Graphics->DrawText(35, cd_y, COLOR2D(0, 0, 0), first_line, C2DGraphics::AlignLeft);

        strncpy(second_line, pISOPathDisplay + first_line_chars, second_line_chars);
        second_line[second_line_chars] = '\0';
        m_Graphics->DrawText(35, cd_y + 20, COLOR2D(0, 0, 0), second_line, C2DGraphics::AlignLeft);

        strncpy(third_line, pISOPathDisplay + first_line_chars + second_line_chars, third_line_chars);
        third_line[third_line_chars] = '\0';
        m_Graphics->DrawText(35, cd_y + 40, COLOR2D(0, 0, 0), third_line, C2DGraphics::AlignLeft);
    }

    // Use the helper function to draw navigation bar (false = main screen layout)
    DrawNavigationBar("main");

    // Update the display
    m_Graphics->UpdateDisplay();

    // Ensure display stays on
    m_Display->On();
}

// TODO: put in common place
void ST7789HomePage::DrawNavigationBar(const char* screenType) {
    // Draw button bar at bottom
    m_Graphics->DrawRect(0, 210, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));

    // --- A BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(5, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(5, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "A" using lines instead of text
    unsigned a_x = 14;   // Center of A
    unsigned a_y = 225;  // Center of button

    // Draw A using thick lines (3px wide)
    // Left diagonal of A
    m_Graphics->DrawLine(a_x - 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 5, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 3, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));

    // Right diagonal of A
    m_Graphics->DrawLine(a_x + 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x + 5, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x + 3, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));

    // Middle bar of A
    m_Graphics->DrawLine(a_x - 2, a_y, a_x + 2, a_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 2, a_y + 1, a_x + 2, a_y + 1, COLOR2D(0, 0, 0));  // Fixed: a_y+1 instead of a_x+1

    // UP arrow for navigation screens or custom icon for main screen
    unsigned arrow_x = 35;
    unsigned arrow_y = 225;

    if (strcmp(screenType, "main") == 0) {
        unsigned cd_x = 35;
        unsigned cd_y = 215;
        unsigned cd_radius = 10;
        // Draw outer circle of CD
        m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(255, 255, 255));

        // Draw middle circle of CD
        m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(255, 255, 255));

        // Draw center hole of CD
        m_Graphics->DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(255, 255, 255));
    } else {
        // On other screens, show up navigation arrow
        // Stem (3px thick)
        m_Graphics->DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));

        // Arrow head
        m_Graphics->DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    }

    // --- B BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(65, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(65, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "B" using lines instead of text
    unsigned b_x = 74;   // Center of B
    unsigned b_y = 225;  // Center of button

    // Draw B using thick lines
    // Vertical line of B
    m_Graphics->DrawLine(b_x - 3, b_y - 6, b_x - 3, b_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x - 2, b_y - 6, b_x - 2, b_y + 6, COLOR2D(0, 0, 0));

    // Top curve of B
    m_Graphics->DrawLine(b_x - 3, b_y - 6, b_x + 2, b_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y - 6, b_x + 3, b_y - 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y - 5, b_x + 3, b_y - 1, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y - 1, b_x + 2, b_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y, b_x - 2, b_y, COLOR2D(0, 0, 0));

    // Bottom curve of B
    m_Graphics->DrawLine(b_x - 3, b_y + 6, b_x + 2, b_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y + 6, b_x + 3, b_y + 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y + 5, b_x + 3, b_y + 1, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y + 1, b_x + 2, b_y, COLOR2D(0, 0, 0));

    // Thicker parts - reinforce
    m_Graphics->DrawLine(b_x - 1, b_y - 5, b_x + 1, b_y - 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x - 1, b_y + 5, b_x + 1, b_y + 5, COLOR2D(0, 0, 0));

    // Down arrow for all screens
    arrow_x = 95;
    arrow_y = 225;

    if (strcmp(screenType, "main") == 0) {
        // CHANGED: Show CD icon for B button (same as A button)
        unsigned cd_x = 85;  // Adjusted X position for B button
        unsigned cd_y = 215;
        unsigned cd_radius = 10;
        
        // Draw outer circle of CD
        m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, cd_radius, COLOR2D(255, 255, 255));

        // Draw middle circle of CD
        m_Graphics->DrawCircleOutline(cd_x + cd_radius, cd_y + cd_radius, 5, COLOR2D(255, 255, 255));

        // Draw center hole of CD
        m_Graphics->DrawCircle(cd_x + cd_radius, cd_y + cd_radius, 2, COLOR2D(255, 255, 255));

        // Old power icon for B button
        /*
        int radius = 7;            // Adjust radius as needed
        int cx = arrow_x + radius;  // Center x
        int cy = arrow_y;          // Center y

        // Draw the circle outline
        m_Graphics->DrawCircleOutline(cx, cy, radius, COLOR2D(255, 255, 255));

        // Draw the vertical line ("I") - top center of the circle downward
        int line_length = 6;  // Length of the line
        m_Graphics->DrawLine(cx, cy - radius - 2, cx, cy - radius + line_length, COLOR2D(255, 255, 255));
        */
    } else {
        // Stem (3px thick)
        m_Graphics->DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x - 1, arrow_y, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 1, arrow_y, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255));

        // Arrow head
        m_Graphics->DrawLine(arrow_x - 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    }

    // --- X BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(125, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(125, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "X" using lines instead of text
    unsigned x_x = 134;  // Center of X
    unsigned x_y = 225;  // Center of button

    // Draw X using thick lines (3px wide)
    // First diagonal of X (top-left to bottom-right)
    m_Graphics->DrawLine(x_x - 4, x_y - 6, x_x + 4, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x - 5, x_y - 6, x_x + 3, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x - 3, x_y - 6, x_x + 5, x_y + 6, COLOR2D(0, 0, 0));

    // Second diagonal of X (top-right to bottom-left)
    m_Graphics->DrawLine(x_x + 4, x_y - 6, x_x - 4, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x + 5, x_y - 6, x_x - 3, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x + 3, x_y - 6, x_x - 5, x_y + 6, COLOR2D(0, 0, 0));

    // Icon next to X button - different based on screen type
    unsigned icon_x = 155;
    unsigned icon_y = 220;

    if (strcmp(screenType, "main") == 0) {
        // CHANGED: Show menu bars for X button (was power icon)
        // Menu bars for main screen
        // Thicker menu bars (2px)
        m_Graphics->DrawLine(icon_x, icon_y - 5, icon_x + 15, icon_y - 5, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y - 4, icon_x + 15, icon_y - 4, COLOR2D(255, 255, 255));

        m_Graphics->DrawLine(icon_x, icon_y, icon_x + 15, icon_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y + 1, icon_x + 15, icon_y + 1, COLOR2D(255, 255, 255));

        m_Graphics->DrawLine(icon_x, icon_y + 5, icon_x + 15, icon_y + 5, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y + 6, icon_x + 15, icon_y + 6, COLOR2D(255, 255, 255));
    } else {
        // Red X icon for other screens (cancel)
        m_Graphics->DrawLine(icon_x - 8, icon_y - 8, icon_x + 8, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 8, icon_y - 8, icon_x - 8, icon_y + 8, COLOR2D(255, 0, 0));

        // Make red X thicker
        m_Graphics->DrawLine(icon_x - 7, icon_y - 8, icon_x + 7, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 7, icon_y - 8, icon_x - 7, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x - 8, icon_y - 7, icon_x + 8, icon_y + 7, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 8, icon_y - 7, icon_x - 8, icon_y + 7, COLOR2D(255, 0, 0));
    }

    // --- Y BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(185, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(185, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "Y" using lines instead of text
    unsigned y_x = 194;  // Center of Y
    unsigned y_y = 225;  // Center of button

    // Draw Y using thick lines (3px wide)
    // Upper left diagonal of Y
    m_Graphics->DrawLine(y_x - 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 5, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 3, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));

    // Upper right diagonal of Y
    m_Graphics->DrawLine(y_x + 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 5, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 3, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));

    // Stem of Y
    m_Graphics->DrawLine(y_x, y_y, y_x, y_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 1, y_y, y_x - 1, y_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 1, y_y, y_x + 1, y_y + 6, COLOR2D(0, 0, 0));

    // Draw small hammer icon in the header
    unsigned hammer_x = 215;
    unsigned hammer_y = 225;

    // Draw hammer head (claw hammer shape)
    // Main head body
    m_Graphics->DrawRect(hammer_x - 7, hammer_y - 4, 10, 6, COLOR2D(255, 255, 255));

    // Claw part (back of hammer)
    m_Graphics->DrawRect(hammer_x - 9, hammer_y - 3, 3, 2, COLOR2D(255, 255, 255));
    m_Graphics->DrawRect(hammer_x - 10, hammer_y - 2, 2, 2, COLOR2D(255, 255, 255));

    // Strike face (front of hammer)
    m_Graphics->DrawRect(hammer_x + 3, hammer_y - 3, 2, 4, COLOR2D(255, 255, 255));

    // Draw hammer handle (wooden grip)
    m_Graphics->DrawRect(hammer_x - 1, hammer_y + 2, 2, 8, COLOR2D(255, 255, 255));

    // Add grip texture lines
    m_Graphics->DrawLine(hammer_x - 1, hammer_y + 4, hammer_x, hammer_y + 4, COLOR2D(58, 124, 165));
    m_Graphics->DrawLine(hammer_x - 1, hammer_y + 6, hammer_x, hammer_y + 6, COLOR2D(58, 124, 165));
    m_Graphics->DrawLine(hammer_x - 1, hammer_y + 8, hammer_x, hammer_y + 8, COLOR2D(58, 124, 165));

    /*
    if (strcmp(screenType, "main") == 0) {
        // Folder icon for main screen
        m_Graphics->DrawRect(y_icon_x, y_icon_y - 2, 16, 11, COLOR2D(255, 255, 255));
        m_Graphics->DrawRect(y_icon_x + 2, y_icon_y - 5, 8, 4, COLOR2D(255, 255, 255));
    } else {
        // GREEN CHECKMARK for all other screens
        // Draw a green checkmark
        // Shorter part of checkmark
        m_Graphics->DrawLine(y_icon_x - 8, y_icon_y, y_icon_x - 3, y_icon_y + 5, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 8, y_icon_y + 1, y_icon_x - 3, y_icon_y + 6, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 7, y_icon_y, y_icon_x - 2, y_icon_y + 5, COLOR2D(0, 255, 0));

        // Longer part of checkmark
        m_Graphics->DrawLine(y_icon_x - 3, y_icon_y + 5, y_icon_x + 8, y_icon_y - 6, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 3, y_icon_y + 6, y_icon_x + 8, y_icon_y - 5, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 2, y_icon_y + 5, y_icon_x + 7, y_icon_y - 4, COLOR2D(0, 255, 0));
    }
    */
}

void ST7789HomePage::TruncatePathWithEllipsis(const char* fullPath, char* outBuffer, size_t outBufferSize, size_t maxChars) {
    size_t fullLen = strlen(fullPath);

    // If path fits, just copy it
    if (fullLen <= maxChars) {
        strncpy(outBuffer, fullPath, outBufferSize - 1);
        outBuffer[outBufferSize - 1] = '\0';
        return;
    }

    // Split path into components and truncate each as needed
    // Strategy: preserve folder structure, truncate individual components with "..."

    // First, count components and their lengths
    char tempPath[MAX_PATH_LEN];
    strncpy(tempPath, fullPath, MAX_PATH_LEN - 1);
    tempPath[MAX_PATH_LEN - 1] = '\0';

    // Count total components
    int numComponents = 1;
    for (const char* p = tempPath; *p; p++) {
        if (*p == '/') numComponents++;
    }

    // Calculate max chars per component (leaving room for slashes and "...")
    // Reserve 3 chars for "..." per component that needs truncation
    size_t charsForSlashes = numComponents - 1;
    size_t availableChars = maxChars - charsForSlashes;
    size_t maxPerComponent = availableChars / numComponents;
    if (maxPerComponent < 6) maxPerComponent = 6;  // Minimum: "ab...z"

    // Build output by processing each component
    outBuffer[0] = '\0';
    char* token = strtok(tempPath, "/");
    bool first = true;

    while (token != NULL) {
        if (!first) {
            strncat(outBuffer, "/", outBufferSize - strlen(outBuffer) - 1);
        }
        first = false;

        size_t tokenLen = strlen(token);
        if (tokenLen <= maxPerComponent) {
            // Component fits - add as-is
            strncat(outBuffer, token, outBufferSize - strlen(outBuffer) - 1);
        } else {
            // Component needs truncation
            // Format: first few chars + "..." + last few chars
            size_t showChars = maxPerComponent - 3;  // Account for "..."
            size_t frontChars = showChars / 2;
            size_t backChars = showChars - frontChars;

            // Add front part
            size_t currentLen = strlen(outBuffer);
            if (currentLen + frontChars < outBufferSize - 1) {
                strncat(outBuffer, token, frontChars);
            }

            // Add ellipsis
            strncat(outBuffer, "...", outBufferSize - strlen(outBuffer) - 1);

            // Add back part
            if (backChars > 0 && tokenLen >= backChars) {
                strncat(outBuffer, token + tokenLen - backChars, outBufferSize - strlen(outBuffer) - 1);
            }
        }

        token = strtok(NULL, "/");
    }

    outBuffer[outBufferSize - 1] = '\0';
}
