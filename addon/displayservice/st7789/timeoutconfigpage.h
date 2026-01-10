#ifndef _ST7789_TOCONFIGPAGE_H
#define _ST7789_TOCONFIGPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <display/st7789display.h>
#include <configservice/configservice.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class ST7789TimeoutConfigPage : public IPage {
public:
    ST7789TimeoutConfigPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789TimeoutConfigPage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

private:
    void Draw();
    void ScrollUp();
    void ScrollDown();
    void SelectItem();
    void DrawNavigationBar(const char* screenType);
    void DrawConfirmation(const char* message);
    void MoveSelection(int delta);

private:
    bool m_ShouldChangePage = false;
    const char* m_NextPageName;
    CST7789Display*          m_Display;
    C2DGraphics*             m_Graphics;
    ConfigService* configservice;
    const char* options[7] = { "5s", "10s", "30s", "60s", "2 min", "5 min", "Never" };
    const unsigned timeoutValues[7] = { 5, 10, 30, 60, 120, 300, 0 };
    size_t m_SelectedIndex = 0;

};
#endif
