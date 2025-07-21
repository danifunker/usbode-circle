#ifndef _ST7789_INFO_H
#define _ST7789_INFO_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <display/st7789display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class ST7789InfoPage : public IPage {
public:
    ST7789InfoPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789InfoPage();
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
    CST7789Display*          m_Display;
    C2DGraphics*             m_Graphics;
    size_t m_SelectedIndex = 0;

};
#endif
