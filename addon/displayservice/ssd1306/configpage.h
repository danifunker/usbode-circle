#ifndef _SSD1306_CONFIGPAGE_H
#define _SSD1306_CONFIGPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <libssd1306/ssd1306display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SSD1306ConfigPage : public IPage {
public:
    SSD1306ConfigPage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306ConfigPage();
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
    CSSD1306GfxDisplay*          m_Display;
    C2DGraphics*             m_Graphics;
    const char* options[7] = { "USB Config", "Logging Config", "Timeout Config", "Sound Config", "USB Target OS", "Build Info", "Power Menu" };
    size_t m_SelectedIndex = 0;

};
#endif
