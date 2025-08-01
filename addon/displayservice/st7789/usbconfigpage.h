#ifndef _ST7789_USBCONFIGPAGE_H
#define _ST7789_USBCONFIGPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <display/st7789display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class ST7789USBConfigPage : public IPage {
   public:
    ST7789USBConfigPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789USBConfigPage();
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
    CST7789Display* m_Display;
    C2DGraphics* m_Graphics;
    ConfigService* config;
    const char* options[2] = {"High Speed", "Full Speed"};
    size_t m_SelectedIndex = 0;
};
#endif
