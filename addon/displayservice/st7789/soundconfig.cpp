#include "soundconfig.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/configservice.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>
#include <cstring>

LOGMODULE("soundconfig");

ST7789SoundConfigPage::ST7789SoundConfigPage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
}

ST7789SoundConfigPage::~ST7789SoundConfigPage() {
    LOGNOTE("SoundConfigPage starting");
}

void ST7789SoundConfigPage::OnEnter() {
    LOGNOTE("Drawing SoundConfigPage");
    const char* currentSoundDev = config->GetSoundDev("none");
    if (strcmp(currentSoundDev, "sndi2s") == 0) {
        m_SelectedIndex = 0;
    } else if (strcmp(currentSoundDev, "sndpwm") == 0) {
        m_SelectedIndex = 1;
    } else if (strcmp(currentSoundDev, "sndhdmi") == 0) {
        m_SelectedIndex = 2;
    } else {
        m_SelectedIndex = 3;
    }
    Draw();
}

void ST7789SoundConfigPage::OnExit() {
    m_ShouldChangePage = false;
}

bool ST7789SoundConfigPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789SoundConfigPage::nextPageName() {
    return "configpage";
}

void ST7789SoundConfigPage::OnButtonPress(Button button) {
    LOGNOTE("Button received by page %d", button);

    switch (button) {
        case Button::Up:
            LOGNOTE("Move Up");
            MoveSelection(-1);
            break;

        case Button::Down:
            LOGNOTE("Move Down");
            MoveSelection(+1);
            break;

        case Button::Ok:
        case Button::Center:
            switch (m_SelectedIndex) {
                case 0:
                    LOGNOTE("Setting i2s audio");
                    config->SetSoundDev("sndi2s");
                    LOGNOTE("Saved config");
                    SaveAndReboot();
                    break;
                case 1:
                    LOGNOTE("Setting PWM audio");
                    config->SetSoundDev("sndpwm");
                    SaveAndReboot();
                    break;
                case 2:
                    LOGNOTE("Setting HDMI audio");
                    config->SetSoundDev("sndhdmi");
                    SaveAndReboot();
                    break;
                case 3:
                    LOGNOTE("Disabling Audio");
                    config->SetSoundDev("none");
                    SaveAndReboot();
                    break;
            }
        break;

        case Button::Cancel:
            LOGNOTE("Cancel");
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
}

void ST7789SoundConfigPage::MoveSelection(int delta) {
    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    LOGDBG("Selected index is %d, Menu delta is %d", m_SelectedIndex, delta);
    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = 0;
    else if (newIndex >= static_cast<int>(fileCount))
        newIndex = static_cast<int>(fileCount - 1);

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
        LOGDBG("New menu index is %d", newIndex);
        m_SelectedIndex = static_cast<size_t>(newIndex);
        Draw();
    }
}

void ST7789SoundConfigPage::Refresh() {
    // Nothing to do here
}

void ST7789SoundConfigPage::SaveAndReboot() {
    DrawConfirmation("Saved, rebooting...");
    // We have to assume the save operation worked. We can't trigger the save
    // from this interrupt. We have to finish the interrupt operation before the
    // file io can happen
    new CShutdown(ShutdownReboot, 1000);
}
void ST7789SoundConfigPage::DrawConfirmation(const char* message) {
    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    // Draw header bar with blue background
    const char* pTitle = "Sound Config";
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    m_Graphics->DrawText(10, 40, COLOR2D(0, 0, 0), message, C2DGraphics::AlignLeft);
    m_Graphics->UpdateDisplay();
}

void ST7789SoundConfigPage::Draw() {
    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    // Draw header bar with blue background
    const char* pTitle = "Sound Config";
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    const char* currentSoundDev = config->GetSoundDev("none");
    char currentText[64];
    if (strcmp(currentSoundDev, "sndi2s") == 0) {
        strcpy(currentText, "Current: I2S Audio");
    } else if (strcmp(currentSoundDev, "sndpwm") == 0) {
        strcpy(currentText, "Current: PWM Audio");
    } else if (strcmp(currentSoundDev, "sndhdmi") == 0) {
        strcpy(currentText, "Current: HDMI Audio");
    } else {
        strcpy(currentText, "Current: Disabled");
    }
    m_Graphics->DrawText(10, 40, COLOR2D(0, 0, 0), currentText, C2DGraphics::AlignLeft);

    size_t startIndex = 0;
    size_t endIndex = fileCount;

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 20);
        const char* name = options[i];

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 58, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
            m_Graphics->DrawText(10, y + 60, COLOR2D(255, 255, 255), name, C2DGraphics::AlignLeft);
        } else {
            m_Graphics->DrawText(10, y + 60, COLOR2D(0, 0, 0), name, C2DGraphics::AlignLeft);
        }
    }

    DrawNavigationBar("config");
    m_Graphics->UpdateDisplay();
}
// TODO: put in common place
void ST7789SoundConfigPage::DrawNavigationBar(const char* screenType) {
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
