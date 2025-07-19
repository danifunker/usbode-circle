#ifndef _ST7789_HOMEPAGE_H
#define _ST7789_HOMEPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <display/st7789display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>
#include <scsitbservice/scsitbservice.h>

class ST7789HomePage : public IPage {
public:
    ST7789HomePage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789HomePage();
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
    const char* GetIPAddress();
    const char* GetCurrentImage();
    const char* GetVersionString();
    const char* GetUSBSpeed();
    const char* m_NextPageName;

private:
    bool m_ShouldChangePage = false;
    CST7789Display*          m_Display;
    C2DGraphics*             m_Graphics;
    SCSITBService* m_Service = nullptr;
    char* pIPAddress;
    char* pISOName;
    char* pUSBSpeed;
};
#endif
