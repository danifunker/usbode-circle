#include "setuppage.h"
#include <setupstatus/setupstatus.h>
#include <circle/logger.h>
#include <string.h>

LOGMODULE("sh1106setuppage");

SH1106SetupPage::SH1106SetupPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display), m_Graphics(graphics) {
    m_statusText[0] = '\0';
}

SH1106SetupPage::~SH1106SetupPage() {}

void SH1106SetupPage::OnEnter() {
    m_ShouldChangePage = false;
    m_refreshCounter = 0;
    strncpy(m_statusText, "Setting up device", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
    Draw();
}

void SH1106SetupPage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106SetupPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106SetupPage::nextPageName() {
    return "homepage";
}

void SH1106SetupPage::OnButtonPress(Button buttonId) {
    // Ignore buttons during setup
}

void SH1106SetupPage::Refresh() {
    m_refreshCounter++;

    SetupStatus* setupStatus = SetupStatus::Get();
    char newStatus[sizeof(m_statusText)];
    if (!setupStatus) {
        strncpy(newStatus, "Setup unavailable", sizeof(newStatus) - 1);
        newStatus[sizeof(newStatus) - 1] = '\0';
    } else {
        const char* statusMsg = setupStatus->getStatusMessage();
        if (statusMsg && statusMsg[0]) {
            strncpy(newStatus, statusMsg, sizeof(newStatus) - 1);
            newStatus[sizeof(newStatus) - 1] = '\0';
        } else {
            strncpy(newStatus, "Setting up device", sizeof(newStatus) - 1);
            newStatus[sizeof(newStatus) - 1] = '\0';
        }
    }

    // Redraw the whole screen if the status text changed or every 8 ticks for animation
    if (strncmp(m_statusText, newStatus, sizeof(m_statusText)) != 0 ||
        (m_refreshCounter % 8 == 0)) {
        strncpy(m_statusText, newStatus, sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
        Draw();
    }
}

void SH1106SetupPage::Draw() {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));

    // Header bar
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Setup", C2DGraphics::AlignLeft, Font8x8);

    // Status text (like infopage, y=16)
    m_Graphics->DrawText(4, 16, COLOR2D(255, 255, 255), m_statusText, C2DGraphics::AlignLeft, Font6x7);

    // Wait message (y=28, clear and visible)
    m_Graphics->DrawText(4, 28, COLOR2D(255, 255, 255), "Wait 60 seconds...", C2DGraphics::AlignLeft, Font6x7);

    // Animated dots (y=44, centered)
    int dotY = 44;
    int dotX = 60;
    int numDots = 1 + ((m_refreshCounter / 8) % 3);
    for (int i = 0; i < numDots; ++i) {
        m_Graphics->DrawText(dotX + (i * 8), dotY, COLOR2D(255, 255, 255), ".", C2DGraphics::AlignLeft, Font8x8);
    }

    m_Graphics->UpdateDisplay();
}