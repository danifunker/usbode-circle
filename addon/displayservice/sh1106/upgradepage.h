#ifndef _SH1106_UPGRADEPAGE_H
#define _SH1106_UPGRADEPAGE_H

#include <circle/2dgraphics.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

class SH1106UpgradePage : public IPage {
public:
    SH1106UpgradePage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106UpgradePage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

private:
    void Draw();

    bool m_ShouldChangePage;
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    char m_statusText[64];
    unsigned m_refreshCounter;
};

#endif // _SH1106_UPGRADEPAGE_H