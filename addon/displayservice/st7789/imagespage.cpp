#include "imagespage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <configservice/configservice.h>
#include <cstring>

LOGMODULE("imagespage");

ST7789ImagesPage::ST7789ImagesPage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    CCharGenerator Font(DEFAULT_FONT, CCharGenerator::FontFlagsNone);
    charWidth = Font.GetCharWidth();
    maxTextPx = m_Display->GetWidth() - 20;

    m_CurrentPath[0] = '\0';
}

ST7789ImagesPage::~ST7789ImagesPage() {
}

void ST7789ImagesPage::OnEnter() {
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

void ST7789ImagesPage::OnExit() {
    m_ShouldChangePage = false;
}

bool ST7789ImagesPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* ST7789ImagesPage::nextPageName() {
    return m_NextPageName;
}

void ST7789ImagesPage::OnButtonPress(Button button) {
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

void ST7789ImagesPage::MoveSelection(int delta) {
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

void ST7789ImagesPage::NavigateToFolder(const char* path) {
    if (!path) return;

    strncpy(m_CurrentPath, path, MAX_PATH_LEN - 1);
    m_CurrentPath[MAX_PATH_LEN - 1] = '\0';

    m_SelectedIndex = 0;
    m_MountedIndex = (size_t)-1;
    dirty = true;
}

void ST7789ImagesPage::NavigateUp() {
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
size_t ST7789ImagesPage::GetVisibleCount() {
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
bool ST7789ImagesPage::IsParentDirEntry(size_t visibleIndex) {
    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;
    bool isRoot = (m_CurrentPath[0] == '\0');

    if (!flatFileList && !isRoot && visibleIndex == 0) {
        return true;
    }
    return false;
}

// Returns the cache index for the given visible index
size_t ST7789ImagesPage::GetCacheIndex(size_t visibleIndex) {
    ConfigService* config = ConfigService::Get();
    bool flatFileList = config ? config->GetFlatFileList() : false;
    bool isRoot = (m_CurrentPath[0] == '\0');
    size_t pathLen = strlen(m_CurrentPath);

    // Account for ".." entry
    size_t offset = 0;
    if (!flatFileList && !isRoot) {
        if (visibleIndex == 0) return (size_t)-1;
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
const char* ST7789ImagesPage::GetDisplayName(size_t visibleIndex) {
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

void ST7789ImagesPage::DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
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

void ST7789ImagesPage::DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
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

void ST7789ImagesPage::RefreshScroll() {
    size_t visibleCount = GetVisibleCount();
    if (m_SelectedIndex >= visibleCount) return;

    const char* displayName = GetDisplayName(m_SelectedIndex);
    size_t nameLen = strlen(displayName);
    int fullTextPx = ((int)nameLen + 2) * charWidth;

    if (fullTextPx > maxTextPx) {
        if (m_ScrollDirLeft) {
            m_ScrollOffsetPx += 5;
            if (m_ScrollOffsetPx >= (fullTextPx - maxTextPx)) {
                m_ScrollOffsetPx = (fullTextPx - maxTextPx);
                m_ScrollDirLeft = false;
            }
        } else {
            m_ScrollOffsetPx -= 5;
            if (m_ScrollOffsetPx <= 0) {
                m_ScrollOffsetPx = 0;
                m_ScrollDirLeft = true;
            }
        }

        size_t currentPage = m_SelectedIndex / ITEMS_PER_PAGE;
        size_t startIndex = currentPage * ITEMS_PER_PAGE;
        int y = static_cast<int>((m_SelectedIndex - startIndex) * 20);

        char extended[nameLen + 2];
        snprintf(extended, sizeof(extended), "%s ", displayName);

        m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
        DrawTextScrolled(10, y + 30, COLOR2D(255, 255, 255), extended, m_ScrollOffsetPx);
        m_Graphics->UpdateDisplay();
    }
}

void ST7789ImagesPage::Refresh() {
    if (dirty) {
        Draw();
        return;
    }
    RefreshScroll();
}

void ST7789ImagesPage::Draw() {
    if (!m_Service) return;

    size_t visibleCount = GetVisibleCount();
    if (visibleCount == 0) return;

    dirty = false;

    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    const char* pTitle = "CD Images";

    // Draw header bar with blue background
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

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
        int y = static_cast<int>((i - startIndex) * 20);

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

                    if ((int)filenameLen >= maxLen - 4) {
                        // Filename alone is too long, show ".../filename" truncated
                        snprintf(cropped, sizeof(cropped), ".../%.*s", maxLen - 4, filename);
                    } else {
                        // Show truncated folder + filename: "fold.../file.iso"
                        int availForFolder = maxLen - (int)filenameLen - 4; // 4 = ".../".length
                        if (availForFolder > 0) {
                            snprintf(cropped, sizeof(cropped), "%.*s.../%s", availForFolder, displayName, filename);
                        } else {
                            snprintf(cropped, sizeof(cropped), ".../%s", filename);
                        }
                    }
                } else {
                    snprintf(cropped, sizeof(cropped), "%.*s", maxLen, displayName);
                }
            } else {
                snprintf(cropped, sizeof(cropped), "%.*s", maxLen, displayName);
            }
        } else {
            snprintf(cropped, sizeof(cropped), "%.*s", maxLen, displayName);
        }

        if (isMounted)
            m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 255, 0));

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
            DrawText(10, y + 30, COLOR2D(255, 255, 255), cropped);
        } else {
            DrawText(10, y + 30, COLOR2D(0, 0, 0), cropped);
        }
    }

    RefreshScroll();

    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", (short)currentPage + 1, (short)totalPages);
    m_Graphics->DrawText(180, 10, COLOR2D(255, 255, 255), pageText, C2DGraphics::AlignLeft);

    DrawNavigationBar("images");
    m_Graphics->UpdateDisplay();
}

void ST7789ImagesPage::DrawNavigationBar(const char* screenType) {
    // Draw button bar at bottom
    unsigned int displayHeight = m_Display->GetHeight();
    unsigned int displayWidth = m_Display->GetWidth();

    m_Graphics->DrawRect(0, displayHeight - 30, displayWidth, 30, COLOR2D(58, 124, 165));

    // Left section - Cancel/Back
    unsigned int section_width = displayWidth / 3;

    // Draw X icon for cancel
    unsigned int x_icon_x = section_width / 2;
    unsigned int x_icon_y = displayHeight - 15;
    m_Graphics->DrawLine(x_icon_x - 5, x_icon_y - 5, x_icon_x + 5, x_icon_y + 5, COLOR2D(255, 0, 0));
    m_Graphics->DrawLine(x_icon_x + 5, x_icon_y - 5, x_icon_x - 5, x_icon_y + 5, COLOR2D(255, 0, 0));

    // Center section - OK/Select
    unsigned int ok_icon_x = section_width + section_width / 2;
    unsigned int ok_icon_y = displayHeight - 15;

    // Draw checkmark for OK
    m_Graphics->DrawLine(ok_icon_x - 5, ok_icon_y, ok_icon_x - 2, ok_icon_y + 3, COLOR2D(0, 255, 0));
    m_Graphics->DrawLine(ok_icon_x - 2, ok_icon_y + 3, ok_icon_x + 5, ok_icon_y - 4, COLOR2D(0, 255, 0));

    // Right section - Up/Down navigation
    unsigned int y_icon_x = 2 * section_width + section_width / 2;
    unsigned int y_icon_y = displayHeight - 15;

    // Draw up arrow
    m_Graphics->DrawLine(y_icon_x - 5, y_icon_y - 2, y_icon_x, y_icon_y - 7, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(y_icon_x, y_icon_y - 7, y_icon_x + 5, y_icon_y - 2, COLOR2D(255, 255, 255));

    // Draw down arrow
    m_Graphics->DrawLine(y_icon_x - 5, y_icon_y + 2, y_icon_x, y_icon_y + 7, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(y_icon_x, y_icon_y + 7, y_icon_x + 5, y_icon_y + 2, COLOR2D(255, 255, 255));
}
