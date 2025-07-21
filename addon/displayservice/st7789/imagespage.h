#ifndef _ST7789_IMAGESPAGE_H
#define _ST7789_IMAGESPAGE_H

#include <displayservice/ipage.h>
#include <displayservice/buttons.h>
#include <display/st7789display.h>
#include <circle/spimaster.h>
#include <circle/2dgraphics.h>
#include <scsitbservice/scsitbservice.h>

#define ITEMS_PER_PAGE 9 
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

class ST7789ImagesPage : public IPage {
public:
    ST7789ImagesPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789ImagesPage();
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
    void SetSelectedName(const char* name);
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
    size_t m_TotalFiles;
    size_t m_SelectedIndex = 0;

    int m_ScrollOffset = 0;
    bool m_ScrollDirLeft = true;
    uint32_t m_LastScrollMs = 0;
    int m_PreviousSelectedIndex = -1;
};
#endif
