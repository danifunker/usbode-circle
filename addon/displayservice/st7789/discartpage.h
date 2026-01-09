#ifndef _ST7789_DISCARTPAGE_H
#define _ST7789_DISCARTPAGE_H

#include <circle/2dgraphics.h>
#include <circle/types.h>
#include <display/st7789display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

class ST7789DiscArtPage : public IPage {
public:
    ST7789DiscArtPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789DiscArtPage();

    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    void Draw() override;
    bool shouldChangePage() override;
    const char* nextPageName() override;

    // Set the disc image path to load art for
    void SetDiscImagePath(const char* path);

    // Check if disc art is available for current path
    bool HasArt() const { return m_HasArt; }

private:
    bool LoadArt();
    void FreeArt();

private:
    CST7789Display* m_Display;
    C2DGraphics* m_Graphics;

    char m_DiscImagePath[512];
    u16* m_ArtBuffer;
    bool m_HasArt;
    bool m_ShouldChangePage;
};

#endif // _ST7789_DISCARTPAGE_H
