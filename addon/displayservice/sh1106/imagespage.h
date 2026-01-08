#ifndef _SH1106_IMAGESPAGE_H
#define _SH1106_IMAGESPAGE_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <libsh1106/sh1106display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>
#include <scsitbservice/scsitbservice.h>

#define ITEMS_PER_PAGE 5
#define MAX_FILTERED_ITEMS 128
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

// Entry in the filtered view (represents either ".." or a cache entry)
struct FilteredEntry {
    bool isParentDir;           // True if this is the ".." entry
    size_t cacheIndex;          // Index into m_Service cache (only valid if !isParentDir)
};

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
    void ScrollUp();
    void ScrollDown();
    void SelectItem();
    void DrawNavigationBar(const char* screenType);
    void MoveSelection(int delta);
    void SetSelectedName(const char* name);
    void BuildFilteredList();
    void NavigateToFolder(const char* path);
    void NavigateUp();
    const char* GetIPAddress();
    const char* GetCurrentImage();
    const char* GetVersionString();
    const char* GetUSBSpeed();
    const char* m_NextPageName;

   private:
    bool m_ShouldChangePage = false;
    CSH1106Display* m_Display;
    C2DGraphics* m_Graphics;
    SCSITBService* m_Service = nullptr;
    size_t m_TotalFiles;
    size_t m_SelectedIndex = 0;
    size_t m_MountedIndex = 0;
    int charWidth;
    int maxTextPx;
    bool dirty = false;

    int m_ScrollOffsetPx = 0;
    bool m_ScrollDirLeft = true;
    uint32_t m_LastScrollMs = 0;
    size_t m_PreviousSelectedIndex = -1;

    // Folder navigation state
    char m_CurrentPath[MAX_PATH_LEN];       // Current folder path (e.g., "Games/RPG" or "" for root)
    FilteredEntry m_FilteredList[MAX_FILTERED_ITEMS];  // Filtered entries for current view
    size_t m_FilteredCount = 0;             // Number of entries in filtered list
    void DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                  const TFont& rFont = DEFAULT_FONT,
                  CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);
    void DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                          int pixelOffset, const TFont& rFont = DEFAULT_FONT,
                          CCharGenerator::TFontFlags FontFlags = CCharGenerator::FontFlagsNone);
    void RefreshScroll();
};
#endif
