#ifndef _SSD1306_INFO_H
#define _SSD1306_INFO_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SSD1306InfoPage : public IPage {
   public:
    SSD1306InfoPage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306InfoPage();
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
    CSSD1306GfxDisplay* m_Display;
    C2DGraphics* m_Graphics;
    size_t m_SelectedIndex = 0;
};
#endif
