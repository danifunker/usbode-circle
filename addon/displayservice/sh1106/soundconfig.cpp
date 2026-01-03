#include "soundconfig.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/configservice.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>
#include <cstring>

LOGMODULE("soundconfig");

SH1106SoundConfigPage::SH1106SoundConfigPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
}

SH1106SoundConfigPage::~SH1106SoundConfigPage() {
    LOGNOTE("SoundConfigPage starting");
}

void SH1106SoundConfigPage::OnEnter() {
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

void SH1106SoundConfigPage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106SoundConfigPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106SoundConfigPage::nextPageName() {
    return "configpage";
}

void SH1106SoundConfigPage::OnButtonPress(Button button) {
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

void SH1106SoundConfigPage::MoveSelection(int delta) {
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

void SH1106SoundConfigPage::Refresh() {
    // TODO We shouldn't redraw everything!
    // Draw();
}

void SH1106SoundConfigPage::SaveAndReboot() {
    DrawConfirmation("Saved, rebooting...");
    // We have to assume the save operation worked. We can't trigger the save
    // from this interrupt. We have to finish the interrupt operation before the
    // file io can happen
    new CShutdown(ShutdownReboot, 1000);
}
void SH1106SoundConfigPage::DrawConfirmation(const char* message) {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Sound Config", C2DGraphics::AlignLeft, Font8x8);

    m_Graphics->DrawText(0, 16, COLOR2D(255,255,255), message, C2DGraphics::AlignLeft, Font6x7);
    m_Graphics->UpdateDisplay();
}

void SH1106SoundConfigPage::Draw() {
    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Sound Config", C2DGraphics::AlignLeft, Font8x8);

    size_t startIndex = 0;
    size_t endIndex = fileCount;

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 10);
        const char* name = options[i];

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
            m_Graphics->DrawText(0, y + 16, COLOR2D(0,0,0), name, C2DGraphics::AlignLeft, Font6x7);
        } else {
            m_Graphics->DrawText(0, y + 16, COLOR2D(255,255,255), name, C2DGraphics::AlignLeft, Font6x7);
        }
    }
    m_Graphics->UpdateDisplay();
}
