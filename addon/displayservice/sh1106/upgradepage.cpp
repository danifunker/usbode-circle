#include "upgradepage.h"
#include <upgradestatus/upgradestatus.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <linux/kernel.h>


LOGMODULE("sh1106upgradepage");

SH1106UpgradePage::SH1106UpgradePage(CSH1106Display* display, C2DGraphics* graphics)
    : m_ShouldChangePage(false), m_Display(display), m_Graphics(graphics), m_refreshCounter(0) {
    m_statusText[0] = '\0';
}

SH1106UpgradePage::~SH1106UpgradePage() {
    LOGNOTE("UpgradePage destroyed");
}

void SH1106UpgradePage::OnEnter() {
    LOGNOTE("Drawing UpgradePage");
    m_ShouldChangePage = false;
    m_refreshCounter = 0;
    strncpy(m_statusText, "Upgrading...", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
    Draw();
}

void SH1106UpgradePage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106UpgradePage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106UpgradePage::nextPageName() {
    return "homepage";
}

void SH1106UpgradePage::OnButtonPress(Button buttonId) {
    // No button interaction during upgrade - just ignore
    LOGNOTE("Button received by upgrade page %d (ignored during upgrade)", buttonId);
}

void SH1106UpgradePage::Refresh() {
    m_refreshCounter++;
    
    UpgradeStatus* upgradeStatus = UpgradeStatus::Get();
    const char* statusMsg = upgradeStatus->getStatusMessage();

    if (statusMsg && statusMsg[0]) {
        strncpy(m_statusText, statusMsg, sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
    } else {
        strncpy(m_statusText, "Upgrade in progress...", sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
    }
    
    // Add animation dots (simple text-based animation)
    int dots = (m_refreshCounter / 10) % 4;
    size_t len = strlen(m_statusText);
    if (len < sizeof(m_statusText) - 4) {
        for (int i = 0; i < dots && len < sizeof(m_statusText) - 1; i++) {
            m_statusText[len++] = '.';
        }
        m_statusText[len] = '\0';
    }

    Draw();
}

void SH1106UpgradePage::Draw() {
    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));

    // Draw title at top - use AlignLeft like other SH1106 pages
    const char* title = "System Upgrade";
    m_Graphics->DrawText(10, 0, COLOR2D(255, 255, 255), title, 
                         C2DGraphics::AlignLeft, Font6x7);

    // Draw status message
    m_Graphics->DrawText(5, 15, COLOR2D(255, 255, 255), m_statusText, 
                         C2DGraphics::AlignLeft, Font6x7);

    // Draw progress if available
    UpgradeStatus* upgradeStatus = UpgradeStatus::Get();
    if (upgradeStatus->isUpgradeInProgress()) {
        int current = upgradeStatus->getCurrentProgress();
        int total = upgradeStatus->getTotalProgress();
        if (total > 0) {
            char progressText[32];
            snprintf(progressText, sizeof(progressText), "Progress: %d/%d", current, total);
            m_Graphics->DrawText(5, 30, COLOR2D(255, 255, 255), progressText, 
                               C2DGraphics::AlignLeft, Font6x7);
        }
        
        // Simple spinner animation - positioned to the right
        const char* spinner = "|/-\\";
        char spinnerText[10];
        snprintf(spinnerText, sizeof(spinnerText), "[%c]", spinner[(m_refreshCounter / 5) % 4]);
        m_Graphics->DrawText(5, 45, COLOR2D(255, 255, 255), spinnerText, 
                           C2DGraphics::AlignLeft, Font6x7);
    }

    // Show completion message
    if (upgradeStatus->isUpgradeComplete()) {
        m_Graphics->DrawText(5, 30, COLOR2D(255, 255, 255), "Complete!", 
                           C2DGraphics::AlignLeft, Font6x7);
        m_Graphics->DrawText(5, 45, COLOR2D(255, 255, 255), "Rebooting...", 
                           C2DGraphics::AlignLeft, Font6x7);
    }

    m_Graphics->UpdateDisplay();
}