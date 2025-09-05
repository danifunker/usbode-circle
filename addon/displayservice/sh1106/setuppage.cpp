#include "setuppage.h"
#include <setupstatus/setupstatus.h>
#include <circle/logger.h>

LOGMODULE("sh1106setuppage");

SH1106SetupPage::SH1106SetupPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display), m_Graphics(graphics) {}

SH1106SetupPage::~SH1106SetupPage() {}

void SH1106SetupPage::OnEnter() {
    m_ShouldChangePage = false;
    m_refreshCounter = 0;
    m_statusText = "Setting up device";
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
    if (!setupStatus) {
        m_statusText = "Setup unavailable";
        Draw();
        return;
    }

    const char* statusMsg = setupStatus->getStatusMessage();
    if (statusMsg && statusMsg[0]) {
        m_statusText = statusMsg;
    } else {
        m_statusText = "Setting up device";
    }

    // Redraw every 8 ticks for animation
    if (m_refreshCounter % 8 == 0) {
        Draw();
    }
}

void SH1106SetupPage::Draw() {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));

    // Draw header bar
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Setup", C2DGraphics::AlignLeft, Font8x8);

    // Draw status text (centered vertically)
    int textY = 28;
    m_Graphics->DrawText(4, textY, COLOR2D(255, 255, 255), (const char*)m_statusText, C2DGraphics::AlignLeft, Font6x7);

    // Draw animated progress dots
    DrawProgressDots();

    // Optionally, show "Please wait" at the bottom
    m_Graphics->DrawText(4, 54, COLOR2D(255, 255, 255), "Please wait 60 seconds...", C2DGraphics::AlignLeft, Font6x7);

    m_Graphics->UpdateDisplay();
}

void SH1106SetupPage::DrawProgressDots() {
    // Animate 1-3 dots, cycling every 8 ticks
    int numDots = 1 + ((m_refreshCounter / 8) % 3);
    int dotY = 44;
    int dotX = 60; // Centered for 128px width

    for (int i = 0; i < numDots; ++i) {
        m_Graphics->DrawText(dotX + (i * 8), dotY, COLOR2D(255, 255, 255), ".", C2DGraphics::AlignLeft, Font8x8);
    }
}