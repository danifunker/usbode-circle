#ifndef _SSD1306_CLASSICMACMODEPAGE_H
#define _SSD1306_CLASSICMACMODEPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SSD1306ClassicMacModePage : public IPage {
   public:
    SSD1306ClassicMacModePage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306ClassicMacModePage();
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
    void MoveSelection(int delta);
    void SaveAndReboot();
    bool IsSaved();
    void DrawConfirmation(const char* message);

   private:
    bool m_ShouldChangePage = false;
    CSSD1306GfxDisplay* m_Display;
    C2DGraphics* m_Graphics;
    ConfigService* config;
    const char* options[2] = {"Dos/Win", "Classic MacOS 9.x"};
    size_t m_SelectedIndex = 0;
};
#endif
