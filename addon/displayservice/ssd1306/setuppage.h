#ifndef _SSD1306_SETUPPAGE_H
#define _SSD1306_SETUPPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SSD1306SetupPage : public IPage {
public:
    SSD1306SetupPage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306SetupPage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    bool shouldChangePage() override;
    const char* nextPageName() override;

private:
    void Draw();
    void DrawProgressDots();

    bool m_ShouldChangePage = false;
    CSSD1306GfxDisplay* m_Display;
    C2DGraphics* m_Graphics;
    char m_statusText[64];
    unsigned m_refreshCounter = 0;
};

#endif