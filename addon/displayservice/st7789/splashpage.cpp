#include "splashpage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>

LOGMODULE("splashpage");

ST7789SplashPage::ST7789SplashPage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
}

ST7789SplashPage::~ST7789SplashPage() {
}

void ST7789SplashPage::OnEnter() {
    Draw();
    CScheduler::Get()->MsSleep(2000);  // Length of time to show splash
    m_ShouldChangePage = true;
}

void ST7789SplashPage::OnExit() {
    m_ShouldChangePage = false;
}

bool ST7789SplashPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789SplashPage::nextPageName() {
    return "homepage";
}

void ST7789SplashPage::OnButtonPress(Button button) {
}

void ST7789SplashPage::Refresh() {
}

void ST7789SplashPage::Draw() {
    m_Graphics->DrawImage(0, 0, 240, 240, splashImage);
    m_Graphics->UpdateDisplay();
}
