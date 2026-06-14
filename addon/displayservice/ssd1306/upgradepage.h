#ifndef _SSD1306_UPGRADEPAGE_H
#define _SSD1306_UPGRADEPAGE_H

#include <circle/2dgraphics.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

class SSD1306UpgradePage : public IPage {
public:
    SSD1306UpgradePage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306UpgradePage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

private:
    void Draw();

    bool m_ShouldChangePage;
    CSSD1306GfxDisplay* m_Display;
    C2DGraphics* m_Graphics;
    char m_statusText[64];
    unsigned m_refreshCounter;
};

#endif // _SSD1306_UPGRADEPAGE_H