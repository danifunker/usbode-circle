#ifndef _SH1106_SETUPPAGE_H
#define _SH1106_SETUPPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SH1106SetupPage : public IPage {
public:
    SH1106SetupPage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106SetupPage();
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
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    char m_statusText[64]; // Use a char buffer for status text
    unsigned m_refreshCounter = 0;
};

#endif