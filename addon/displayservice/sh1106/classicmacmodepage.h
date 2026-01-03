#ifndef _SH1106_CLASSICMACMODEPAGE_H
#define _SH1106_CLASSICMACMODEPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SH1106ClassicMacModePage : public IPage {
   public:
    SH1106ClassicMacModePage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106ClassicMacModePage();
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
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    ConfigService* config;
    const char* options[2] = {"Dos/Win", "Classic MacOS 9.x"};
    size_t m_SelectedIndex = 0;
};
#endif
