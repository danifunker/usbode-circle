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

    // Only redraw the whole screen if the status text changed
    if (strncmp(m_statusText, newStatus, sizeof(m_statusText)) != 0) {
        strncpy(m_statusText, newStatus, sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
        Draw();
        return;
    }

    // Always update the dots and "Please wait" message
    // Draw only the dots and message area to avoid flicker
    int dotY = 48;
    int dotX = 60;
    int waitMsgY = 36;

    // Clear the area for dots and wait message
    m_Graphics->DrawRect(0, waitMsgY, m_Display->GetWidth(), 20, COLOR2D(0, 0, 0));

    // Draw "Please wait 60 seconds..." above the dots
    m_Graphics->DrawText(4, waitMsgY, COLOR2D(255, 255, 255), "Please wait 60 seconds...", C2DGraphics::AlignLeft, Font6x7);

    // Draw animated progress dots
    int numDots = 1 + ((m_refreshCounter / 8) % 3);
    for (int i = 0; i < numDots; ++i) {
        m_Graphics->DrawText(dotX + (i * 8), dotY, COLOR2D(255, 255, 255), ".", C2DGraphics::AlignLeft, Font8x8);
    }

    m_Graphics->UpdateDisplay();
}

void SH1106SetupPage::Draw() {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));

    // Draw header bar
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Setup", C2DGraphics::AlignLeft, Font8x8);

    // Draw status text (centered vertically)
    int textY = 20;
    m_Graphics->DrawText(4, textY, COLOR2D(255, 255, 255), m_statusText, C2DGraphics::AlignLeft, Font6x7);

    // Draw "Please wait 60 seconds..." above the dots
    int waitMsgY = 36;
    m_Graphics->DrawText(4, waitMsgY, COLOR2D(255, 255, 255), "Please wait 60 seconds...", C2DGraphics::AlignLeft, Font6x7);

    // Draw animated progress dots
    int dotY = 48;
    int dotX = 60;
    int numDots = 1 + ((m_refreshCounter / 8) % 3);
    for (int i = 0; i < numDots; ++i) {
        m_Graphics->DrawText(dotX + (i * 8), dotY, COLOR2D(255, 255, 255), ".", C2DGraphics::AlignLeft, Font8x8);
    }

    m_Graphics->UpdateDisplay();
}