#include "timeoutconfigpage.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>
#include <linux/kernel.h>

LOGMODULE("timeoutconfigpage");

ST7789TimeoutConfigPage::ST7789TimeoutConfigPage(CST7789Display* display, C2DGraphics* graphics)
: m_Display(display),
  m_Graphics(graphics)
{
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
}

ST7789TimeoutConfigPage::~ST7789TimeoutConfigPage() {
    LOGNOTE("TimeoutConfigPage starting");
}

void ST7789TimeoutConfigPage::OnEnter()
{
    LOGNOTE("Drawing TimeoutConfigPage");

    unsigned currentTimeout = configservice->GetScreenTimeout();
    
    // Reset options array to original state first
    options[0] = "5s";
    options[1] = "10s";
    options[2] = "30s";
    options[3] = "60s";
    options[4] = "2 min";
    options[5] = "5 min";
    options[6] = "Never";
    options[7] = nullptr;
    
    timeoutValues[0] = 5;
    timeoutValues[1] = 10;
    timeoutValues[2] = 30;
    timeoutValues[3] = 60;
    timeoutValues[4] = 120;
    timeoutValues[5] = 300;
    timeoutValues[6] = 0;
    timeoutValues[7] = 0;
    
    // Always show 8 options (7 predefined + 1 custom)
    m_OptionCount = 8;
    
    // Check if current timeout matches any predefined option (including 0 for Never)
    bool foundMatch = false;
    for (size_t i = 0; i < 7; ++i) {
        if (timeoutValues[i] == currentTimeout) {
            foundMatch = true;
            m_SelectedIndex = i;
            break;
        }
    }
    
    // Update custom option display
    if (!foundMatch && currentTimeout > 0) {
        // Show actual custom value (only for non-zero values)
        if (currentTimeout >= 60) {
            snprintf(customLabel, sizeof(customLabel), "Custom: %u min", currentTimeout / 60);
        } else {
            snprintf(customLabel, sizeof(customLabel), "Custom: %us", currentTimeout);
        }
        
        // Find insertion point between 5 min (index 5) and Never (index 6)
        // Custom values go at index 6, Never shifts to index 7
        options[7] = options[6];  // Move Never to position 7
        timeoutValues[7] = timeoutValues[6];
        
        options[6] = customLabel;  // Put custom at position 6
        timeoutValues[6] = currentTimeout;
        m_SelectedIndex = 6;  // Select the custom value
    } else {
        // Show placeholder for custom at the end
        snprintf(customLabel, sizeof(customLabel), "Custom: not set");
        options[7] = customLabel;
        timeoutValues[7] = 0;  // Not used when not set
    }

    Draw();
}

size_t ST7789TimeoutConfigPage::FindClosestTimeout(unsigned currentTimeout)
{
    size_t closestIndex = 0;
    unsigned minDiff = 0xFFFFFFFF;  // Max unsigned value

    for (size_t i = 0; i < 7; ++i) {
        unsigned diff = (currentTimeout > timeoutValues[i]) 
            ? (currentTimeout - timeoutValues[i]) 
            : (timeoutValues[i] - currentTimeout);
        
        if (diff < minDiff) {
            minDiff = diff;
            closestIndex = i;
        }
    }

    return closestIndex;
}

void ST7789TimeoutConfigPage::OnExit()
{
	m_ShouldChangePage = false;
}

bool ST7789TimeoutConfigPage::shouldChangePage() {
	return m_ShouldChangePage;
}

const char* ST7789TimeoutConfigPage::nextPageName() {
	return m_NextPageName;
}

void ST7789TimeoutConfigPage::OnButtonPress(Button button)
{
	LOGNOTE("Button received by page %d", button);
	
    switch (button)
    {
        case Button::Up:
	    LOGNOTE("Move Up");
            MoveSelection(-1);
            break;

        case Button::Down:
	    LOGNOTE("Move Down");
            MoveSelection(+1);
            break;

        case Button::Ok:
	{
	    unsigned timeout = timeoutValues[m_SelectedIndex];
	    LOGNOTE("Setting screen timeout to %d", timeout);
	    configservice->SetScreenTimeout(timeout);
	    m_NextPageName = "homepage";
	    m_ShouldChangePage = true;
            break;
	}

        case Button::Cancel:
	    LOGNOTE("Cancel");
	    m_NextPageName = "configpage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
    
}

void ST7789TimeoutConfigPage::MoveSelection(int delta) {

    if (m_OptionCount == 0) return;

    LOGDBG("Selected index is %d, Menu delta is %d", m_SelectedIndex, delta);
    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = 0;
    else if (newIndex >= static_cast<int>(m_OptionCount))
        newIndex = static_cast<int>(m_OptionCount - 1);

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
	LOGDBG("New menu index is %d", newIndex);
        m_SelectedIndex = static_cast<size_t>(newIndex);
        Draw();
    }
}

void ST7789TimeoutConfigPage::Refresh()
{
}

void ST7789TimeoutConfigPage::Draw() {
    if (m_OptionCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    // Draw header bar with blue background
    const char* pTitle = "Sleep Timeout";
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    size_t startIndex = 0;
    size_t endIndex = m_OptionCount;

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 20);
        const char* name = options[i];

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
            m_Graphics->DrawText(10, y + 30, COLOR2D(255, 255, 255), name, C2DGraphics::AlignLeft);
        } else {
            m_Graphics->DrawText(10, y + 30, COLOR2D(0, 0, 0), name, C2DGraphics::AlignLeft);
        }
    }

    DrawNavigationBar("power");
    m_Graphics->UpdateDisplay();
}

// TODO: put in common place
void ST7789TimeoutConfigPage::DrawNavigationBar(const char* screenType) {
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
        // On main screen, show select icon
        // Stem (3px thick)
        m_Graphics->DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));

        // Arrow head
        m_Graphics->DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
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

    // Stem (3px thick)
    m_Graphics->DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x - 1, arrow_y, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x + 1, arrow_y, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255));

    // Arrow head
    m_Graphics->DrawLine(arrow_x - 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x + 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));

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
    unsigned icon_y = 225;

    if (strcmp(screenType, "main") == 0) {
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

    // Icon next to Y button - different based on screen type
    unsigned y_icon_x = 215;
    unsigned y_icon_y = 225;

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
}
