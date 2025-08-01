#ifndef _SH1106_TOCONFIGPAGE_H
#define _SH1106_TOCONFIGPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <libsh1106/sh1106display.h>
#include <configservice/configservice.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class SH1106TimeoutConfigPage : public IPage {
public:
    SH1106TimeoutConfigPage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106TimeoutConfigPage();
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
    ConfigService* configservice;
    const char* options[5] = { "5s", "10s", "15s", "20s", "25s" };
    size_t m_SelectedIndex = 0;

};
#endif
