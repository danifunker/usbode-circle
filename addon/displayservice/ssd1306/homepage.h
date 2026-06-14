#ifndef _SSD1306_HOMEPAGE_H
#define _SSD1306_HOMEPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <libssd1306/ssd1306display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <scsitbservice/scsitbservice.h>

class SSD1306HomePage : public IPage {
   public:
    SSD1306HomePage(CSSD1306GfxDisplay* display, C2DGraphics* graphics);
    ~SSD1306HomePage();
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
    void GetIPAddress(char* buffer, size_t size);
    const char* GetCurrentImagePath();
    const char* GetVersionString();
    const char* GetUSBSpeed();
    void RefreshISOScroll();
    void DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                          int pixelOffset, const TFont& rFont = Font6x7);
    const char* m_NextPageName;
    ConfigService* config;

   private:
    bool m_ShouldChangePage = false;
    CSSD1306GfxDisplay* m_Display;
    C2DGraphics* m_Graphics;
    SCSITBService* m_Service = nullptr;
    char pIPAddress[16];
    char pISOPath[MAX_PATH_LEN];  // Store full path for scrolling
    const char* pUSBSpeed;
    const char* pTitle;

    // ISO name scrolling state
    int m_ISOScrollOffsetPx = 0;
    bool m_ISOScrollDirLeft = true;
    int m_ISOCharWidth = 6;  // Font6x7 char width
    int m_ISOMaxTextPx = 0;  // Max pixels for ISO display area
};
#endif
