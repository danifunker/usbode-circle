#include "imagespage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/configservice.h>
#include <cstring>

LOGMODULE("imagespage");

SH1106ImagesPage::SH1106ImagesPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    CCharGenerator Font(Font6x7, CCharGenerator::FontFlagsNone);
    charWidth = Font.GetCharWidth();
    maxTextPx = m_Display->GetWidth();

    m_CurrentPath[0] = '\0';
}

SH1106ImagesPage::~SH1106ImagesPage() {
}

void SH1106ImagesPage::OnEnter() {
    LOGNOTE("Drawing imagespage");

    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;

    const char* currentPath = m_Service->GetCurrentCDPath();
    m_CurrentPath[0] = '\0';

    // In folder mode, navigate to the folder containing the current image
    if (!flatFileList && currentPath && currentPath[0] != '\0') {
        const char* pathStart = currentPath;
        if (strncmp(pathStart, "1:/", 3) == 0) {
            pathStart += 3;
        }

        const char* lastSlash = strrchr(pathStart, '/');
        if (lastSlash != nullptr) {
            size_t folderLen = lastSlash - pathStart;
            if (folderLen < MAX_PATH_LEN) {
                strncpy(m_CurrentPath, pathStart, folderLen);
                m_CurrentPath[folderLen] = '\0';
            }
        }
    }

    // Find the currently mounted image
    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;

    const char* currentRelPath = nullptr;
    if (currentPath && currentPath[0] != '\0') {
        currentRelPath = currentPath;
        if (strncmp(currentRelPath, "1:/", 3) == 0) {
            currentRelPath += 3;
        }
    }

    if (currentRelPath) {
        size_t visibleCount = GetVisibleCount();
        for (size_t i = 0; i < visibleCount; ++i) {
            if (!IsParentDirEntry(i)) {
                size_t cacheIdx = GetCacheIndex(i);
                const char* entryPath = m_Service->GetRelativePath(cacheIdx);
                if (entryPath && strcmp(entryPath, currentRelPath) == 0) {
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

    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;

    switch (button) {
        case Button::Up:
            MoveSelection(-1);
            break;

        case Button::Down:
            MoveSelection(+1);
            break;

        case Button::Left:
            MoveSelection(-5);
            break;

        case Button::Right:
            MoveSelection(+5);
            break;

        case Button::Ok:
        case Button::Center:
            if (IsParentDirEntry(m_SelectedIndex)) {
                NavigateUp();
            } else {
                size_t cacheIdx = GetCacheIndex(m_SelectedIndex);
                bool isDir = m_Service->IsDirectory(cacheIdx);
                if (isDir) {
                    const char* relativePath = m_Service->GetRelativePath(cacheIdx);
                    NavigateToFolder(relativePath);
                } else {
                    const char* relativePath = m_Service->GetRelativePath(cacheIdx);
                    m_Service->SetNextCDByName(relativePath);
                    m_MountedIndex = m_SelectedIndex;
                    m_NextPageName = "homepage";
                    m_ShouldChangePage = true;
                }
            }
            break;

        case Button::Cancel:
            if (!flatFileList && m_CurrentPath[0] != '\0') {
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

void SH1106ImagesPage::MoveSelection(int delta) {
    size_t count = GetVisibleCount();
    if (count == 0) return;

    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = static_cast<int>(count - 1);
    else if (newIndex >= static_cast<int>(count))
        newIndex = 0;

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
        m_SelectedIndex = static_cast<size_t>(newIndex);
        dirty = true;
    }
}

void SH1106ImagesPage::NavigateToFolder(const char* path) {
    if (!path) return;

    strncpy(m_CurrentPath, path, MAX_PATH_LEN - 1);
    m_CurrentPath[MAX_PATH_LEN - 1] = '\0';

    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;
    dirty = true;
}

void SH1106ImagesPage::NavigateUp() {
    if (m_CurrentPath[0] == '\0') return;

    char* lastSlash = strrchr(m_CurrentPath, '/');
    if (lastSlash != nullptr) {
        *lastSlash = '\0';
    } else {
        m_CurrentPath[0] = '\0';
    }

    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;
    dirty = true;
}

// Returns how many visible items there are in the current view
size_t SH1106ImagesPage::GetVisibleCount() {
    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;
    bool isRoot = (m_CurrentPath[0] == '\0');
    size_t pathLen = strlen(m_CurrentPath);

    size_t count = 0;

    // Add ".." if not at root and not in flat mode
    if (!flatFileList && !isRoot) {
        count++;
    }

    size_t totalEntries = m_Service->GetCount();
    for (size_t i = 0; i < totalEntries; ++i) {
        const char* entryPath = m_Service->GetRelativePath(i);
        if (!entryPath) continue;

        bool showEntry = false;
        if (flatFileList) {
            showEntry = !m_Service->IsDirectory(i);
        } else if (isRoot) {
            showEntry = (strchr(entryPath, '/') == nullptr);
        } else {
            if (strncmp(entryPath, m_CurrentPath, pathLen) == 0 && entryPath[pathLen] == '/') {
                const char* remainder = entryPath + pathLen + 1;
                showEntry = (strchr(remainder, '/') == nullptr);
            }
        }

        if (showEntry) count++;
    }

    return count;
}

// Returns true if the visible index is the ".." parent directory entry
bool SH1106ImagesPage::IsParentDirEntry(size_t visibleIndex) {
    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;
    bool isRoot = (m_CurrentPath[0] == '\0');

    // ".." is only present if not in flat mode and not at root
    if (!flatFileList && !isRoot && visibleIndex == 0) {
        return true;
    }
    return false;
}

// Returns the cache index for the given visible index
size_t SH1106ImagesPage::GetCacheIndex(size_t visibleIndex) {
    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;
    bool isRoot = (m_CurrentPath[0] == '\0');
    size_t pathLen = strlen(m_CurrentPath);

    // Account for ".." entry
    size_t offset = 0;
    if (!flatFileList && !isRoot) {
        if (visibleIndex == 0) return (size_t)-1;  // ".." entry
        offset = 1;
    }

    size_t targetIndex = visibleIndex - offset;
    size_t count = 0;

    size_t totalEntries = m_Service->GetCount();
    for (size_t i = 0; i < totalEntries; ++i) {
        const char* entryPath = m_Service->GetRelativePath(i);
        if (!entryPath) continue;

        bool showEntry = false;
        if (flatFileList) {
            showEntry = !m_Service->IsDirectory(i);
        } else if (isRoot) {
            showEntry = (strchr(entryPath, '/') == nullptr);
        } else {
            if (strncmp(entryPath, m_CurrentPath, pathLen) == 0 && entryPath[pathLen] == '/') {
                const char* remainder = entryPath + pathLen + 1;
                showEntry = (strchr(remainder, '/') == nullptr);
            }
        }

        if (showEntry) {
            if (count == targetIndex) return i;
            count++;
        }
    }

    return (size_t)-1;
}

// Returns the display name for the given visible index
const char* SH1106ImagesPage::GetDisplayName(size_t visibleIndex) {
    if (IsParentDirEntry(visibleIndex)) {
        return "..";
    }

    size_t cacheIdx = GetCacheIndex(visibleIndex);
    if (cacheIdx == (size_t)-1) return "";

    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;

    if (flatFileList) {
        return m_Service->GetRelativePath(cacheIdx);
    } else {
        return m_Service->GetName(cacheIdx);
    }
}

void SH1106ImagesPage::DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                const TFont& rFont, CCharGenerator::TFontFlags FontFlags) {
    CCharGenerator Font(rFont, FontFlags);

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
        for (unsigned y = 0; y < Font.GetUnderline(); y++) {
            CCharGenerator::TPixelLine Line = Font.GetPixelLine(*pText, y);
            for (unsigned x = 0; x < Font.GetCharWidth(); x++) {
                unsigned finalX = drawX + x;
                if (finalX >= nX && finalX < m_nWidth && (nY + y) < m_nHeight) {
                    if (Font.GetPixel(x, Line)) {
                        m_Graphics->DrawPixel(finalX, nY + y, Color);
                    }
                }
            }
        }

        if (*pText == ' ') {
            drawX += Font.GetCharWidth() / 2;
        } else {
            drawX += Font.GetCharWidth();
        }
    }
}

void SH1106ImagesPage::RefreshScroll() {
    size_t visibleCount = GetVisibleCount();
    if (m_SelectedIndex >= visibleCount) return;

    const char* displayName = GetDisplayName(m_SelectedIndex);
    size_t nameLen = strlen(displayName);
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
        snprintf(extended, sizeof(extended), "%s ", displayName);

        m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
        DrawTextScrolled(0, y + 16, COLOR2D(0, 0, 0), extended, m_ScrollOffsetPx, Font6x7);
        m_Graphics->UpdateDisplay();
    }
}

void SH1106ImagesPage::Refresh() {
    if (dirty) {
        Draw();
        return;
    }
    RefreshScroll();
}

void SH1106ImagesPage::Draw() {
    if (!m_Service) return;

    size_t visibleCount = GetVisibleCount();
    if (visibleCount == 0) return;

    dirty = false;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Images", C2DGraphics::AlignLeft, Font8x8);

    if (m_SelectedIndex != m_PreviousSelectedIndex) {
        m_ScrollOffsetPx = 0;
        m_ScrollDirLeft = true;
        m_PreviousSelectedIndex = m_SelectedIndex;
    }

    size_t totalPages = (visibleCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    size_t currentPage = m_SelectedIndex / ITEMS_PER_PAGE;
    size_t startIndex = currentPage * ITEMS_PER_PAGE;
    size_t endIndex = MIN(startIndex + ITEMS_PER_PAGE, visibleCount);

    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 10);

        const char* displayName = GetDisplayName(i);
        bool isDir = false;
        bool isMounted = false;

        if (!IsParentDirEntry(i)) {
            size_t cacheIdx = GetCacheIndex(i);
            isDir = m_Service->IsDirectory(cacheIdx);
            isMounted = (i == m_MountedIndex && !isDir);
        }

        const int maxLen = maxTextPx / charWidth;
        char cropped[maxLen + 4];

        if (isDir) {
            snprintf(cropped, sizeof(cropped), "%.*s/", maxLen - 1, displayName);
        } else if (flatFileList && i != m_SelectedIndex) {
            // In flat mode for non-selected items, prioritize filename over folder
            size_t nameLen = strlen(displayName);
            if ((int)nameLen > maxLen) {
                // Find the last '/' to get filename
                const char* lastSlash = strrchr(displayName, '/');
                if (lastSlash) {
                    const char* filename = lastSlash + 1;
                    size_t filenameLen = strlen(filename);

                    if ((int)filenameLen >= maxLen - 3) {
                        // Filename alone is too long, just show "...filename" truncated
                        snprintf(cropped, sizeof(cropped), "...%.*s", maxLen - 3, filename);
                    } else {
                        // Show truncated folder + filename: "fol.../file.iso"
                        int availForFolder = maxLen - (int)filenameLen - 4; // 4 = ".../".length
                        if (availForFolder > 0) {
                            snprintf(cropped, sizeof(cropped), "%.*s.../%s", availForFolder, displayName, filename);
                        } else {
                            snprintf(cropped, sizeof(cropped), ".../%s", filename);
                        }
                    }
                } else {
                    snprintf(cropped, sizeof(cropped), "%s%.*s", isMounted ? "*" : "", maxLen, displayName);
                }
            } else {
                snprintf(cropped, sizeof(cropped), "%s%.*s", isMounted ? "*" : "", maxLen, displayName);
            }
        } else {
            snprintf(cropped, sizeof(cropped), "%s%.*s", isMounted ? "*" : "", maxLen, displayName);
        }

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
            DrawText(0, y + 16, COLOR2D(0, 0, 0), cropped, Font6x7);
        } else {
            DrawText(0, y + 16, COLOR2D(255, 255, 255), cropped, Font6x7);
        }
    }

    RefreshScroll();

    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", (short)currentPage + 1, (short)totalPages);
    m_Graphics->DrawText(85, 1, COLOR2D(0, 0, 0), pageText, C2DGraphics::AlignLeft, Font6x7);

    m_Graphics->UpdateDisplay();
}
