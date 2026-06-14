#include "classicmacmodepage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/configservice.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>

LOGMODULE("classicmacmodepage");

SSD1306ClassicMacModePage::SSD1306ClassicMacModePage(CSSD1306GfxDisplay* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    config = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
}

SSD1306ClassicMacModePage::~SSD1306ClassicMacModePage() {
    LOGNOTE("ClassicMacModePage starting");
}

void SSD1306ClassicMacModePage::OnEnter() {
    LOGNOTE("Drawing ClassicMacModePage");
    USBTargetOS currentOS = config->GetUSBTargetOS();
    m_SelectedIndex = (currentOS == USBTargetOS::Apple) ? 1 : 0;
    Draw();
}

void SSD1306ClassicMacModePage::OnExit() {
    m_ShouldChangePage = false;
}

bool SSD1306ClassicMacModePage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SSD1306ClassicMacModePage::nextPageName() {
    return "configpage";
}

void SSD1306ClassicMacModePage::OnButtonPress(Button button) {
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
            // TODO show an acknowledgement screen rather then just doing
            switch (m_SelectedIndex) {
                case 0:
                    LOGNOTE("Enabling DosWin Mode");
                    config->SetUSBTargetOS(USBTargetOS::DosWin);
                    LOGNOTE("Saved config");
                    SaveAndReboot();
                    break;
                case 1:
                    LOGNOTE("Enabling Classic Mac Mode");
                    config->SetUSBTargetOS(USBTargetOS::Apple);
                    LOGNOTE("Saved config");
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

void SSD1306ClassicMacModePage::MoveSelection(int delta) {
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

void SSD1306ClassicMacModePage::Refresh() {
    // TODO We shouldn't redraw everything!
    // Draw();
}

void SSD1306ClassicMacModePage::SaveAndReboot() {
    DrawConfirmation("Saved, rebooting...");
    // We have to assume the save operation worked. We can't trigger the save
    // from this interrupt. We have to finish the interrupt operation before the
    // file io can happen
    new CShutdown(ShutdownReboot, 1000);
}

void SSD1306ClassicMacModePage::DrawConfirmation(const char* message) {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "USB Target OS", C2DGraphics::AlignLeft, Font8x8);

    m_Graphics->DrawText(0, 16, COLOR2D(255,255,255), message, C2DGraphics::AlignLeft, Font6x7);
    m_Graphics->UpdateDisplay();
}

void SSD1306ClassicMacModePage::Draw() {
    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "USB Target OS", C2DGraphics::AlignLeft, Font8x8);

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

