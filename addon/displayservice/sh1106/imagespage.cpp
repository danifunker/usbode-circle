#include "imagespage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>
#include <cstring>

LOGMODULE("imagespage");

SH1106ImagesPage::SH1106ImagesPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    // Initialize some variables
    CCharGenerator Font(Font6x7, CCharGenerator::FontFlagsNone);
    charWidth = Font.GetCharWidth();
    maxTextPx = m_Display->GetWidth() - 10;

    // Initialize folder state
    m_CurrentPath[0] = '\0';
    m_FilteredCount = 0;
}

SH1106ImagesPage::~SH1106ImagesPage() {
    LOGNOTE("Imagespage starting");
}

void SH1106ImagesPage::OnEnter() {
    LOGNOTE("Drawing imagespage");

    // Determine initial folder from current mounted image
    const char* currentPath = m_Service->GetCurrentCDPath();
    m_CurrentPath[0] = '\0';

    if (currentPath && currentPath[0] != '\0') {
        // Skip "1:/" prefix if present
        const char* pathStart = currentPath;
        if (strncmp(pathStart, "1:/", 3) == 0) {
            pathStart += 3;
        }

        // Extract folder part (everything before last '/')
        const char* lastSlash = strrchr(pathStart, '/');
        if (lastSlash != nullptr) {
            size_t folderLen = lastSlash - pathStart;
            if (folderLen < MAX_PATH_LEN) {
                strncpy(m_CurrentPath, pathStart, folderLen);
                m_CurrentPath[folderLen] = '\0';
            }
        }
    }

    LOGNOTE("SH1106ImagesPage: Starting in folder '%s'", m_CurrentPath);

    BuildFilteredList();

    // Find mounted image in filtered list
    m_MountedIndex = 0;
    const char* currentName = m_Service->GetCurrentCDName();
    if (currentName) {
        for (size_t i = 0; i < m_FilteredCount; ++i) {
            if (!m_FilteredList[i].isParentDir) {
                const char* entryName = m_Service->GetName(m_FilteredList[i].cacheIndex);
                if (strcmp(entryName, currentName) == 0) {
                    m_MountedIndex = i;
                    m_SelectedIndex = i;
                    break;
                }
            }
        }
    }

    Draw();
}

void SH1106ImagesPage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106ImagesPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106ImagesPage::nextPageName() {
    return m_NextPageName;
}

void SH1106ImagesPage::OnButtonPress(Button button) {
    LOGDBG("Button received by page %d", button);
    switch (button) {
        case Button::Up:
            LOGDBG("Move Up");
            MoveSelection(-1);
            break;

        case Button::Down:
            LOGDBG("Move Down");
            MoveSelection(+1);
            break;

        case Button::Left:
            LOGDBG("Scroll Left");
            MoveSelection(-5);
            break;

        case Button::Right:
            LOGDBG("Scroll Right");
            MoveSelection(+5);
            break;

        case Button::Ok:
        case Button::Center:
            if (m_SelectedIndex < m_FilteredCount) {
                FilteredEntry& entry = m_FilteredList[m_SelectedIndex];

                if (entry.isParentDir) {
                    // Navigate up to parent folder
                    LOGDBG("Navigate to parent");
                    NavigateUp();
                } else {
                    // Check if it's a folder or file
                    bool isDir = m_Service->IsDirectory(entry.cacheIndex);
                    if (isDir) {
                        // Navigate into folder
                        const char* relativePath = m_Service->GetRelativePath(entry.cacheIndex);
                        LOGDBG("Navigate into folder: %s", relativePath);
                        NavigateToFolder(relativePath);
                    } else {
                        // Mount the file using its relative path
                        const char* relativePath = m_Service->GetRelativePath(entry.cacheIndex);
                        LOGDBG("Mounting image: %s", relativePath);
                        m_Service->SetNextCDByName(relativePath);
                        m_MountedIndex = m_SelectedIndex;
                        m_NextPageName = "homepage";
                        m_ShouldChangePage = true;
                    }
                }
            }
            break;

        case Button::Cancel:
            LOGDBG("Cancel");
            // If not at root, go up one level; otherwise exit to homepage
            if (m_CurrentPath[0] != '\0') {
                NavigateUp();
            } else {
                m_NextPageName = "homepage";
                m_ShouldChangePage = true;
            }
            break;

        default:
            break;
    }
}

void SH1106ImagesPage::SetSelectedName(const char* name) {
    if (!m_Service || !name) return;

    size_t fileCount = m_Service->GetCount();
    for (size_t i = 0; i < fileCount; ++i) {
        const char* currentName = m_Service->GetName(i);
        if (strcmp(currentName, name) == 0) {
            m_SelectedIndex = i;
            return;
        }
    }

    // Optional: fallback to 0 if name not found
    m_SelectedIndex = 0;
}

void SH1106ImagesPage::MoveSelection(int delta) {
    if (!m_Service) return;

    if (m_FilteredCount == 0) return;

    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = static_cast<int>(m_FilteredCount - 1);  // Wrap to last item
    else if (newIndex >= static_cast<int>(m_FilteredCount))
        newIndex = 0;                                      // Wrap to first item

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
        m_SelectedIndex = static_cast<size_t>(newIndex);
        dirty = true;
    }
}

void SH1106ImagesPage::DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                const TFont& rFont,
                                CCharGenerator::TFontFlags FontFlags) {
    CCharGenerator Font(rFont, FontFlags);

    unsigned nWidth = 0;
    for (const char* p = pText; *p != '\0'; ++p) {
        nWidth += (*p == ' ') ? (Font.GetCharWidth() / 2) : Font.GetCharWidth();
    }

    for (; *pText != '\0'; ++pText) {
        for (unsigned y = 0; y < Font.GetUnderline(); y++) {
            CCharGenerator::TPixelLine Line = Font.GetPixelLine(*pText, y);

            for (unsigned x = 0; x < Font.GetCharWidth(); x++) {
                if (Font.GetPixel(x, Line)) {
                    m_Graphics->DrawPixel(nX + x, nY + y, Color);
                }
            }
        }

        if (*pText == ' ') {
            nX += Font.GetCharWidth() / 2;
        } else {
            nX += Font.GetCharWidth();
        }
    }
}

void SH1106ImagesPage::DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                        int pixelOffset, const TFont& rFont,
                                        CCharGenerator::TFontFlags FontFlags) {
    CCharGenerator Font(rFont, FontFlags);

    unsigned m_nWidth = m_Graphics->GetWidth();
    unsigned m_nHeight = m_Graphics->GetHeight();
    unsigned drawX = nX - pixelOffset;

    for (; *pText != '\0'; ++pText) {
        // Draw character
        for (unsigned y = 0; y < Font.GetUnderline(); y++) {
            CCharGenerator::TPixelLine Line = Font.GetPixelLine(*pText, y);
            for (unsigned x = 0; x < Font.GetCharWidth(); x++) {
                unsigned finalX = drawX + x;
                if (finalX >= 0 && finalX < m_nWidth && (nY + y) < m_nHeight) {
                    if (Font.GetPixel(x, Line)) {
                        m_Graphics->DrawPixel(finalX, nY + y, Color);
                    }
                }
            }
        }

        if (*pText == ' ') {
            drawX += Font.GetCharWidth() / 2;  // Try /2 or fixed value like 2
        } else {
            drawX += Font.GetCharWidth();
        }
    }
}

void SH1106ImagesPage::RefreshScroll() {
    if (m_SelectedIndex >= m_FilteredCount) return;

    const char* name;
    if (m_FilteredList[m_SelectedIndex].isParentDir) {
        name = "..";
    } else {
        name = m_Service->GetName(m_FilteredList[m_SelectedIndex].cacheIndex);
    }

    size_t nameLen = strlen(name);
    int fullTextPx = ((int)nameLen + 2) * charWidth;

    if (fullTextPx > maxTextPx) {

        if (m_ScrollDirLeft) {
            m_ScrollOffsetPx += 3;
            if (m_ScrollOffsetPx >= (fullTextPx - maxTextPx)) {
                m_ScrollOffsetPx = (fullTextPx - maxTextPx);
                m_ScrollDirLeft = false;
            }
        } else {
            m_ScrollOffsetPx -= 3;
            if (m_ScrollOffsetPx <= 0) {
                m_ScrollOffsetPx = 0;
                m_ScrollDirLeft = true;
            }
        }

        size_t currentPage = m_SelectedIndex / ITEMS_PER_PAGE;
        size_t startIndex = currentPage * ITEMS_PER_PAGE;
        int y = static_cast<int>((m_SelectedIndex - startIndex) * 10);

        char extended[nameLen + 2];
        snprintf(extended, sizeof(extended), "%s ", name);

        m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
        DrawTextScrolled(0, y + 16, COLOR2D(0, 0, 0), extended, m_ScrollOffsetPx, Font6x7);
        m_Graphics->UpdateDisplay();
    }
}

void SH1106ImagesPage::Refresh() {


    // Redraw the screen only when it's needed
    if (dirty) {
	Draw();
	return;
    }

    // Scroll the current line if it needs it
    RefreshScroll();
}

void SH1106ImagesPage::Draw() {
    if (!m_Service) return;

    if (m_FilteredCount == 0) return;

    dirty = false;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Images", C2DGraphics::AlignLeft, Font8x8);

    if (m_SelectedIndex != m_PreviousSelectedIndex) {
        m_ScrollOffsetPx = 0;
        m_ScrollDirLeft = true;
        m_PreviousSelectedIndex = m_SelectedIndex;
    }

    size_t totalPages = (m_FilteredCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    size_t currentPage = m_SelectedIndex / ITEMS_PER_PAGE;
    size_t startIndex = currentPage * ITEMS_PER_PAGE;
    size_t endIndex = MIN(startIndex + ITEMS_PER_PAGE, m_FilteredCount);

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 10);

        const char* name;
        bool isDir = false;
        bool isMounted = false;

        if (m_FilteredList[i].isParentDir) {
            name = "..";
        } else {
            size_t cacheIdx = m_FilteredList[i].cacheIndex;
            name = m_Service->GetName(cacheIdx);
            isDir = m_Service->IsDirectory(cacheIdx);
            isMounted = (i == m_MountedIndex && !isDir);
        }

        // Crop and format display name
        const int maxLen = maxTextPx / charWidth;
        char cropped[maxLen + 2];
        if (isDir) {
            // Show folder with trailing /
            snprintf(cropped, sizeof(cropped), "%.*s/", maxLen - 1, name);
        } else {
            snprintf(cropped, sizeof(cropped), "%s%.*s", isMounted ? "*" : "", maxLen, name);
        }

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
            DrawText(0, y + 16, COLOR2D(0, 0, 0), cropped, Font6x7);
        } else {
            DrawText(0, y + 16, COLOR2D(255, 255, 255), cropped, Font6x7);
        }
    }

    RefreshScroll();

    // Draw page indicator
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", (short)currentPage + 1, (short)totalPages);
    m_Graphics->DrawText(85, 1, COLOR2D(0, 0, 0), pageText, C2DGraphics::AlignLeft, Font6x7);

    m_Graphics->UpdateDisplay();
}

void SH1106ImagesPage::BuildFilteredList() {
    m_FilteredCount = 0;

    bool isRoot = (m_CurrentPath[0] == '\0');
    size_t pathLen = strlen(m_CurrentPath);

    // Add ".." entry if not at root
    if (!isRoot && m_FilteredCount < MAX_FILTERED_ITEMS) {
        m_FilteredList[m_FilteredCount].isParentDir = true;
        m_FilteredList[m_FilteredCount].cacheIndex = 0;  // Not used for parent dir
        m_FilteredCount++;
    }

    // Filter entries from cache
    size_t totalEntries = m_Service->GetCount();
    for (size_t i = 0; i < totalEntries && m_FilteredCount < MAX_FILTERED_ITEMS; ++i) {
        const char* entryPath = m_Service->GetRelativePath(i);
        if (!entryPath) continue;

        bool showEntry = false;
        if (isRoot) {
            // Root: show entries with no '/' in their path
            showEntry = (strchr(entryPath, '/') == nullptr);
        } else {
            // Subfolder: show entries that start with "path/" and have no additional '/'
            if (strncmp(entryPath, m_CurrentPath, pathLen) == 0 && entryPath[pathLen] == '/') {
                const char* remainder = entryPath + pathLen + 1;
                showEntry = (strchr(remainder, '/') == nullptr);
            }
        }

        if (showEntry) {
            m_FilteredList[m_FilteredCount].isParentDir = false;
            m_FilteredList[m_FilteredCount].cacheIndex = i;
            m_FilteredCount++;
        }
    }

    LOGNOTE("SH1106ImagesPage::BuildFilteredList() path='%s', count=%d", m_CurrentPath, (int)m_FilteredCount);
}

void SH1106ImagesPage::NavigateToFolder(const char* path) {
    if (!path) return;

    strncpy(m_CurrentPath, path, MAX_PATH_LEN - 1);
    m_CurrentPath[MAX_PATH_LEN - 1] = '\0';

    BuildFilteredList();
    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;  // No mounted item in this view
    dirty = true;
}

void SH1106ImagesPage::NavigateUp() {
    if (m_CurrentPath[0] == '\0') return;  // Already at root

    // Find last '/' and truncate
    char* lastSlash = strrchr(m_CurrentPath, '/');
    if (lastSlash != nullptr) {
        *lastSlash = '\0';
    } else {
        // No slash found, go to root
        m_CurrentPath[0] = '\0';
    }

    BuildFilteredList();
    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;  // No mounted item in this view
    dirty = true;
}

