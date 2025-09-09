#include "upgradepage.h"
#include <upgradestatus/upgradestatus.h>
#include <circle/logger.h>
#include <circle/util.h> 

LOGMODULE("upgradepage");

ST7789UpgradePage::ST7789UpgradePage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
}

ST7789UpgradePage::~ST7789UpgradePage() {
    LOGNOTE("UpgradePage destroyed");
}

void ST7789UpgradePage::OnEnter() {
    LOGNOTE("Drawing UpgradePage");
    m_ShouldChangePage = false;
    m_refreshCounter = 0;
    m_statusText = "Upgrading...";
    Draw();
}

void ST7789UpgradePage::OnExit() {
    m_ShouldChangePage = false;
}

bool ST7789UpgradePage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789UpgradePage::nextPageName() {
    return "homepage"; // Match the pattern used by other pages
}

void ST7789UpgradePage::OnButtonPress(Button buttonId) {
    // No button interaction during upgrade - just ignore
    // Could add cancel functionality if needed
    LOGNOTE("Button received by upgrade page %d (ignored during upgrade)", buttonId);
}

void ST7789UpgradePage::Refresh() {
    m_refreshCounter++;
    
    UpgradeStatus* upgradeStatus = UpgradeStatus::Get();
    const char* statusMsg = upgradeStatus->getStatusMessage();

    CString statusString = statusMsg ? statusMsg : "";
        
    if (statusString.GetLength() > 0) {
        m_statusText = statusString;
    } else {
        m_statusText = "Upgrade in progress...";
    }
    
    // Add animation dots
    int dots = (m_refreshCounter / 10) % 4;
    for (int i = 0; i < dots; i++) {
        m_statusText += ".";
    }

    // Redraw every few refresh cycles to show animation
    //if (m_refreshCounter % 10 == 0) {
        Draw();
    //}
}

void ST7789UpgradePage::Draw() {
    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    // Draw header bar with blue background (same as other pages)
    const char* pTitle = "System Upgrade";
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    // Draw main status text
    m_Graphics->DrawText(10, 50, COLOR2D(0, 0, 0), (const char*)m_statusText, C2DGraphics::AlignLeft);

    // Draw progress bar if upgrade is in progress
    UpgradeStatus* upgradeStatus = UpgradeStatus::Get();
    if (upgradeStatus->isUpgradeInProgress()) {
        int current = upgradeStatus->getCurrentProgress();
        int total = upgradeStatus->getTotalProgress();
        if (total > 0) {
            DrawProgressBar(current, total);
        }
    }

    // Draw a simple spinner/activity indicator
    if (upgradeStatus->isUpgradeInProgress()) {
        int spinnerFrame = (m_refreshCounter / 5) % 8;
        int centerX = m_Display->GetWidth() / 2;
        int centerY = 120;
        
        // Draw spinning circle segments
        for (int i = 0; i < 8; i++) {
            int alpha = (i == spinnerFrame) ? 255 : (i == ((spinnerFrame - 1) % 8)) ? 128 : 64;
            T2DColor color = COLOR2D(alpha, alpha, alpha);
            
            // Simple dot pattern in circle - simplified without cos/sin
            int x = centerX;
            int y = centerY;
            
            // Simple 8-position circle using if statements
            if (i == 0) { x += 20; }
            else if (i == 1) { x += 14; y -= 14; }
            else if (i == 2) { y -= 20; }
            else if (i == 3) { x -= 14; y -= 14; }
            else if (i == 4) { x -= 20; }
            else if (i == 5) { x -= 14; y += 14; }
            else if (i == 6) { y += 20; }
            else if (i == 7) { x += 14; y += 14; }
            
            m_Graphics->DrawRect(x-2, y-2, 4, 4, color);
        }
    }

    // Show additional info if upgrade is complete
    if (upgradeStatus->isUpgradeComplete()) {
        m_Graphics->DrawText(10, 150, COLOR2D(0, 128, 0), "Upgrade completed successfully!", C2DGraphics::AlignLeft);
        m_Graphics->DrawText(10, 170, COLOR2D(0, 0, 0), "Device will reboot shortly...", C2DGraphics::AlignLeft);
    }

    DrawNavigationBar("upgrade");
    m_Graphics->UpdateDisplay();
}

void ST7789UpgradePage::DrawProgressBar(int current, int total) {
    if (total <= 0) return;
    
    int barWidth = 200;
    int barHeight = 20;
    int barX = (m_Display->GetWidth() - barWidth) / 2;
    int barY = 90;
    
    // Draw progress bar background
    m_Graphics->DrawRect(barX, barY, barWidth, barHeight, COLOR2D(220, 220, 220));
    m_Graphics->DrawRectOutline(barX, barY, barWidth, barHeight, COLOR2D(0, 0, 0));
    
    // Draw progress fill
    int fillWidth = (barWidth * current) / total;
    if (fillWidth > 0) {
        m_Graphics->DrawRect(barX + 1, barY + 1, fillWidth - 2, barHeight - 2, COLOR2D(58, 124, 165));
    }
    
    CString progressText;
    progressText.Format("%d/%d", current, total);
    int textX = barX + (barWidth / 2) - 15; // Rough center
    int textY = barY + 5;
    m_Graphics->DrawText(textX, textY, COLOR2D(255, 255, 255), (const char*)progressText, C2DGraphics::AlignLeft);
}

void ST7789UpgradePage::DrawNavigationBar(const char* screenType) {
    // Draw button bar at bottom (same style as other pages)
    m_Graphics->DrawRect(0, 210, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    
    // Show different messages based on state
    UpgradeStatus* upgradeStatus = UpgradeStatus::Get();
    if (upgradeStatus->isUpgradeComplete()) {
        m_Graphics->DrawText(10, 218, COLOR2D(255, 255, 255), "Rebooting - please wait...", C2DGraphics::AlignLeft);
    } else {
        m_Graphics->DrawText(10, 218, COLOR2D(255, 255, 255), "Please wait - upgrade in progress...", C2DGraphics::AlignLeft);
    }
}
