#ifndef _SH1106_CONFIGPAGE_H
#define _STSH1106_CONFIGPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <display/sh1106display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SH1106ConfigPage : public IPage {
public:
    SH1106ConfigPage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106ConfigPage();
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
    CSH1106Display*          m_Display;
    C2DGraphics*             m_Graphics;
    const char* options[2] = { "USB Config", "Logging Config" };
    size_t m_SelectedIndex = 0;

};
#endif
