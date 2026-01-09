#include "discartpage.h"

#include <circle/logger.h>
#include <discart/discart.h>
#include <string.h>

LOGMODULE("discartpage");

ST7789DiscArtPage::ST7789DiscArtPage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics),
      m_ArtBuffer(nullptr),
      m_HasArt(false),
      m_ShouldChangePage(false) {
    m_DiscImagePath[0] = '\0';
}

ST7789DiscArtPage::~ST7789DiscArtPage() {
    FreeArt();
}

void ST7789DiscArtPage::SetDiscImagePath(const char* path) {
    if (path) {
        strncpy(m_DiscImagePath, path, sizeof(m_DiscImagePath) - 1);
        m_DiscImagePath[sizeof(m_DiscImagePath) - 1] = '\0';
    } else {
        m_DiscImagePath[0] = '\0';
    }
    m_HasArt = false;
}

bool ST7789DiscArtPage::LoadArt() {
    FreeArt();

    if (m_DiscImagePath[0] == '\0') {
        LOGNOTE("No disc image path set");
        return false;
    }

    // Check if disc art exists
    if (!DiscArt::HasDiscArt(m_DiscImagePath)) {
        LOGNOTE("No disc art for: %s", m_DiscImagePath);
        return false;
    }

    // Allocate buffer for RGB565 image data
    m_ArtBuffer = new u16[DISCART_WIDTH * DISCART_HEIGHT];
    if (!m_ArtBuffer) {
        LOGERR("Failed to allocate disc art buffer");
        return false;
    }

    // Load the disc art
    if (!DiscArt::LoadDiscArtRGB565(m_DiscImagePath, m_ArtBuffer)) {
        LOGERR("Failed to load disc art");
        FreeArt();
        return false;
    }

    m_HasArt = true;
    LOGNOTE("Disc art loaded successfully");
    return true;
}

void ST7789DiscArtPage::FreeArt() {
    if (m_ArtBuffer) {
        delete[] m_ArtBuffer;
        m_ArtBuffer = nullptr;
    }
    m_HasArt = false;
}

void ST7789DiscArtPage::OnEnter() {
    m_ShouldChangePage = false;

    // Load the disc art when entering the page
    if (!LoadArt()) {
        // No art available, go to homepage
        LOGNOTE("No disc art available, switching to homepage");
        m_ShouldChangePage = true;
    } else {
        Draw();
    }
}

void ST7789DiscArtPage::OnExit() {
    m_ShouldChangePage = false;
    // Free the art buffer to reclaim memory (~115KB)
    // It will be reloaded on next entry if needed
    FreeArt();
}

void ST7789DiscArtPage::OnButtonPress(Button button) {
    // Any button press goes to homepage
    m_ShouldChangePage = true;
}

void ST7789DiscArtPage::Refresh() {
    // No dynamic content to refresh
}

void ST7789DiscArtPage::Draw() {
    if (m_HasArt && m_ArtBuffer) {
        // Draw the full-screen disc art
        m_Graphics->DrawImage(0, 0, DISCART_WIDTH, DISCART_HEIGHT, m_ArtBuffer);
        m_Graphics->UpdateDisplay();
    } else {
        // Fallback: clear screen with black
        m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
        m_Graphics->UpdateDisplay();
    }
}

bool ST7789DiscArtPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789DiscArtPage::nextPageName() {
    return "homepage";
}
