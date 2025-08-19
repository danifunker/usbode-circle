#ifndef _ST7789_SETUPPAGE_H
#define _ST7789_SETUPPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <display/st7789display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <circle/string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class ST7789SetupPage : public IPage {
public:
    ST7789SetupPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789SetupPage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

private:
    void Draw();
    void DrawNavigationBar(const char* screenType);
    void DrawProgressBar(int current, int total);

private:
    bool m_ShouldChangePage = false;
    CST7789Display* m_Display;
    C2DGraphics* m_Graphics;
    CString m_statusText;
    unsigned m_refreshCounter;
    bool m_setupStarted;
};

#endif // _ST7789_SETUPPAGE_H