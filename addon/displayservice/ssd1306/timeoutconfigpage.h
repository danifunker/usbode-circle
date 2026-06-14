#ifndef _SSD1306_TOCONFIGPAGE_H
#define _SSD1306_TOCONFIGPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <libssd1306/ssd1306display.h>
#include <configservice/configservice.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SSD1306TimeoutConfigPage : public IPage {
public:
    SSD1306TimeoutConfigPage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306TimeoutConfigPage();
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
    size_t FindClosestTimeout(unsigned currentTimeout);

private:
    bool m_ShouldChangePage = false;
    const char* m_NextPageName;
    CSSD1306GfxDisplay*          m_Display;
    C2DGraphics*             m_Graphics;
    ConfigService* configservice;
    const char* options[8] = { "5s", "10s", "30s", "60s", "2 min", "5 min", "Never", nullptr };
    unsigned timeoutValues[8] = { 5, 10, 30, 60, 120, 300, 0, 0 };
    char customLabel[32];
    size_t m_OptionCount = 7;
    size_t m_SelectedIndex = 0;

};
#endif
