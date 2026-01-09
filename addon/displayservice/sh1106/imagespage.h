#ifndef _SH1106_IMAGESPAGE_H
#define _SH1106_IMAGESPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <scsitbservice/scsitbservice.h>

#define ITEMS_PER_PAGE 5
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

class SH1106ImagesPage : public IPage {
   public:
    SH1106ImagesPage(CSH1106Display* display, C2DGraphics* graphics);
    ~SH1106ImagesPage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

   private:
    void Draw();
    void MoveSelection(int delta);
    void NavigateToFolder(const char* path);
    void NavigateUp();

    // Helper functions to iterate visible entries on-the-fly
    size_t GetVisibleCount();
    bool IsParentDirEntry(size_t visibleIndex);
    size_t GetCacheIndex(size_t visibleIndex);  // Returns cache index for visible item
    const char* GetDisplayName(size_t visibleIndex);  // Returns name or path based on flat mode

    void DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                  const TFont& rFont = DEFAULT_FONT,
                  CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);
    void DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                          int pixelOffset, const TFont& rFont = DEFAULT_FONT,
                          CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);
    void RefreshScroll();

    const char* m_NextPageName;
    bool m_ShouldChangePage = false;
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    SCSITBService* m_Service = nullptr;
    size_t m_SelectedIndex = 0;
    size_t m_MountedIndex = 0;
    int charWidth;
    int maxTextPx;
    bool dirty = false;

    int m_ScrollOffsetPx = 0;
    bool m_ScrollDirLeft = true;
    size_t m_PreviousSelectedIndex = -1;

    // Folder navigation state
    char m_CurrentPath[MAX_PATH_LEN];  // Current folder path (e.g., "Games/RPG" or "" for root)
};
#endif
