#ifndef _SH1106_POWERPAGE_H
#define _SH1106_POWERPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <udisplay/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SH1106PowerPage : public IPage {
   public:
    SH1106PowerPage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106PowerPage();
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
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    const char* options[2] = {"Shutdown", "Reboot"};
    size_t m_SelectedIndex = 0;
};
#endif
