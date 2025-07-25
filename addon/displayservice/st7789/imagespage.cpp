#include "imagespage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>

LOGMODULE("imagespage");

ST7789ImagesPage::ST7789ImagesPage(CST7789Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    // Initialize some variables
    CCharGenerator Font(DEFAULT_FONT, CCharGenerator::FontFlagsNone);
    charWidth = Font.GetCharWidth();
    maxTextPx = m_Display->GetWidth() - 10;
}

ST7789ImagesPage::~ST7789ImagesPage() {
    LOGNOTE("Imagespage starting");
}

void ST7789ImagesPage::OnEnter() {
    LOGNOTE("Drawing imagespage");
    const char* name = m_Service->GetCurrentCDName();
    SetSelectedName(name);
    m_MountedIndex = m_SelectedIndex;
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
    switch (button) {
        case Button::Up:
            LOGDBG("Move Up");
            MoveSelection(-1);
            break;

        case Button::Down:
            LOGDBG("Move Down");
            MoveSelection(+1);
            break;

        case Button::Ok:
            LOGDBG("Select new CD %d", m_SelectedIndex);
            // TODO show an acknowledgement screen rather then just returning to main screen
            m_Service->SetNextCD(m_SelectedIndex);
            m_MountedIndex = m_SelectedIndex;
            m_NextPageName = "homepage";
            m_ShouldChangePage = true;
            break;

        case Button::Cancel:
            LOGDBG("Cancel");
            m_NextPageName = "homepage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
}

void ST7789ImagesPage::SetSelectedName(const char* name) {
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

void ST7789ImagesPage::MoveSelection(int delta) {
    if (!m_Service) return;

    size_t fileCount = m_Service->GetCount();
    if (fileCount == 0) return;

    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = 0;
    else if (newIndex >= static_cast<int>(fileCount))
        newIndex = static_cast<int>(fileCount - 1);

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
        LOGDBG("%s", m_Service->GetName(newIndex));
        m_SelectedIndex = static_cast<size_t>(newIndex);
	//Draw();
	dirty = true;
    }
}

void ST7789ImagesPage::DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                const TFont& rFont,
                                CCharGenerator::TFontFlags FontFlags) {
    CCharGenerator Font(rFont, FontFlags);
    int m_nWidth = m_Graphics->GetWidth();
    int m_nHeight = m_Graphics->GetHeight();

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

void ST7789ImagesPage::DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
                                        int pixelOffset, const TFont& rFont,
                                        CCharGenerator::TFontFlags FontFlags) {
    CCharGenerator Font(rFont, FontFlags);

    int m_nWidth = m_Graphics->GetWidth();
    int m_nHeight = m_Graphics->GetHeight();
    unsigned drawX = nX - pixelOffset;

    for (; *pText != '\0'; ++pText) {
        // Draw character
        for (unsigned y = 0; y < Font.GetUnderline(); y++) {
            CCharGenerator::TPixelLine Line = Font.GetPixelLine(*pText, y);
            for (unsigned x = 0; x < Font.GetCharWidth(); x++) {
                int finalX = drawX + x;
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

void ST7789ImagesPage::RefreshScroll() {
    const char* name = m_Service->GetName(m_SelectedIndex);
    size_t nameLen = strlen(name);


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
        snprintf(extended, sizeof(extended), "%s ", name);

        m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
        DrawTextScrolled(10, y + 30, COLOR2D(255, 255, 255), extended, m_ScrollOffsetPx);
	m_Graphics->UpdateDisplay();
    }
}

void ST7789ImagesPage::Refresh() {


    // Redraw the screen only when it's needed
    if (dirty) {
	Draw();
	return;
    }

    // Scroll the current line if it needs it
    RefreshScroll();
}

void ST7789ImagesPage::Draw() {
    if (!m_Service) return;

    size_t fileCount = m_Service->GetCount();
    if (fileCount == 0) return;

    dirty = false;

    m_Graphics->ClearScreen(COLOR2D(255, 255, 255));

    const char* pTitle = CGitInfo::Get()->GetShortVersionString();

    // Draw header bar with blue background
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));

    // Draw title text in white
    m_Graphics->DrawText(10, 8, COLOR2D(255, 255, 255), pTitle, C2DGraphics::AlignLeft);

    if (m_SelectedIndex != m_PreviousSelectedIndex) {
        m_ScrollOffsetPx = 0;
        m_ScrollDirLeft = true;
        m_PreviousSelectedIndex = m_SelectedIndex;
    }

    size_t totalPages = (fileCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    size_t currentPage = m_SelectedIndex / ITEMS_PER_PAGE;
    size_t startIndex = currentPage * ITEMS_PER_PAGE;
    size_t endIndex = MIN(startIndex + ITEMS_PER_PAGE, fileCount);

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 20);
        const char* name = m_Service->GetName(i);
        size_t nameLen = strlen(name);
        char extended[nameLen + 2];
        snprintf(extended, sizeof(extended), "%s ", name);

        // Crop
        const int maxLen = maxTextPx / charWidth;
        char cropped[maxLen + 1];
        snprintf(cropped, sizeof(cropped), "%.*s", maxLen, name);

        // Only scroll selected line and only if too long
        if (i == m_MountedIndex)
            m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 255, 0));

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 28, m_Display->GetWidth(), 22, COLOR2D(0, 0, 0));
            DrawText(10, y + 30, COLOR2D(255,255,255), cropped);
        } else {
            DrawText(10, y + 30, COLOR2D(0, 0, 0), cropped);
        }
    }

    RefreshScroll();

    // Draw page indicator
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", currentPage + 1, totalPages);
    m_Graphics->DrawText(180, 10, COLOR2D(255, 255, 255), pageText, C2DGraphics::AlignLeft);

    DrawNavigationBar("images");
    m_Graphics->UpdateDisplay();
}

// TODO: put in common place
void ST7789ImagesPage::DrawNavigationBar(const char* screenType) {
    // Draw button bar at bottom
    m_Graphics->DrawRect(0, 210, m_Display->GetWidth(), 30, COLOR2D(58, 124, 165));

    // --- A BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(5, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(5, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "A" using lines instead of text
    unsigned a_x = 14;   // Center of A
    unsigned a_y = 225;  // Center of button

    // Draw A using thick lines (3px wide)
    // Left diagonal of A
    m_Graphics->DrawLine(a_x - 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 5, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 3, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));

    // Right diagonal of A
    m_Graphics->DrawLine(a_x + 4, a_y + 6, a_x, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x + 5, a_y + 6, a_x + 1, a_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x + 3, a_y + 6, a_x - 1, a_y - 6, COLOR2D(0, 0, 0));

    // Middle bar of A
    m_Graphics->DrawLine(a_x - 2, a_y, a_x + 2, a_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(a_x - 2, a_y + 1, a_x + 2, a_y + 1, COLOR2D(0, 0, 0));  // Fixed: a_y+1 instead of a_x+1

    // UP arrow for navigation screens or custom icon for main screen
    unsigned arrow_x = 35;
    unsigned arrow_y = 225;

    if (strcmp(screenType, "main") == 0) {
        // On main screen, show select icon
        // Stem (3px thick)
        m_Graphics->DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));

        // Arrow head
        m_Graphics->DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    } else {
        // On other screens, show up navigation arrow
        // Stem (3px thick)
        m_Graphics->DrawLine(arrow_x, arrow_y - 13, arrow_x, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x - 1, arrow_y - 13, arrow_x - 1, arrow_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 1, arrow_y - 13, arrow_x + 1, arrow_y, COLOR2D(255, 255, 255));

        // Arrow head
        m_Graphics->DrawLine(arrow_x - 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(arrow_x + 7, arrow_y - 6, arrow_x, arrow_y - 13, COLOR2D(255, 255, 255));
    }
    // --- B BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(65, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(65, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "B" using lines instead of text
    unsigned b_x = 74;   // Center of B
    unsigned b_y = 225;  // Center of button

    // Draw B using thick lines
    // Vertical line of B
    m_Graphics->DrawLine(b_x - 3, b_y - 6, b_x - 3, b_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x - 2, b_y - 6, b_x - 2, b_y + 6, COLOR2D(0, 0, 0));

    // Top curve of B
    m_Graphics->DrawLine(b_x - 3, b_y - 6, b_x + 2, b_y - 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y - 6, b_x + 3, b_y - 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y - 5, b_x + 3, b_y - 1, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y - 1, b_x + 2, b_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y, b_x - 2, b_y, COLOR2D(0, 0, 0));

    // Bottom curve of B
    m_Graphics->DrawLine(b_x - 3, b_y + 6, b_x + 2, b_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 2, b_y + 6, b_x + 3, b_y + 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y + 5, b_x + 3, b_y + 1, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x + 3, b_y + 1, b_x + 2, b_y, COLOR2D(0, 0, 0));

    // Thicker parts - reinforce
    m_Graphics->DrawLine(b_x - 1, b_y - 5, b_x + 1, b_y - 5, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(b_x - 1, b_y + 5, b_x + 1, b_y + 5, COLOR2D(0, 0, 0));

    // Down arrow for all screens
    arrow_x = 95;
    arrow_y = 225;

    // Stem (3px thick)
    m_Graphics->DrawLine(arrow_x, arrow_y, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x - 1, arrow_y, arrow_x - 1, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x + 1, arrow_y, arrow_x + 1, arrow_y + 13, COLOR2D(255, 255, 255));

    // Arrow head
    m_Graphics->DrawLine(arrow_x - 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));
    m_Graphics->DrawLine(arrow_x + 7, arrow_y + 6, arrow_x, arrow_y + 13, COLOR2D(255, 255, 255));

    // --- X BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(125, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(125, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "X" using lines instead of text
    unsigned x_x = 134;  // Center of X
    unsigned x_y = 225;  // Center of button

    // Draw X using thick lines (3px wide)
    // First diagonal of X (top-left to bottom-right)
    m_Graphics->DrawLine(x_x - 4, x_y - 6, x_x + 4, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x - 5, x_y - 6, x_x + 3, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x - 3, x_y - 6, x_x + 5, x_y + 6, COLOR2D(0, 0, 0));

    // Second diagonal of X (top-right to bottom-left)
    m_Graphics->DrawLine(x_x + 4, x_y - 6, x_x - 4, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x + 5, x_y - 6, x_x - 3, x_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(x_x + 3, x_y - 6, x_x - 5, x_y + 6, COLOR2D(0, 0, 0));

    // Icon next to X button - different based on screen type
    unsigned icon_x = 155;
    unsigned icon_y = 225;

    if (strcmp(screenType, "main") == 0) {
        // Menu bars for main screen
        // Thicker menu bars (2px)
        m_Graphics->DrawLine(icon_x, icon_y - 5, icon_x + 15, icon_y - 5, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y - 4, icon_x + 15, icon_y - 4, COLOR2D(255, 255, 255));

        m_Graphics->DrawLine(icon_x, icon_y, icon_x + 15, icon_y, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y + 1, icon_x + 15, icon_y + 1, COLOR2D(255, 255, 255));

        m_Graphics->DrawLine(icon_x, icon_y + 5, icon_x + 15, icon_y + 5, COLOR2D(255, 255, 255));
        m_Graphics->DrawLine(icon_x, icon_y + 6, icon_x + 15, icon_y + 6, COLOR2D(255, 255, 255));
    } else {
        // Red X icon for other screens (cancel)
        m_Graphics->DrawLine(icon_x - 8, icon_y - 8, icon_x + 8, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 8, icon_y - 8, icon_x - 8, icon_y + 8, COLOR2D(255, 0, 0));

        // Make red X thicker
        m_Graphics->DrawLine(icon_x - 7, icon_y - 8, icon_x + 7, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 7, icon_y - 8, icon_x - 7, icon_y + 8, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x - 8, icon_y - 7, icon_x + 8, icon_y + 7, COLOR2D(255, 0, 0));
        m_Graphics->DrawLine(icon_x + 8, icon_y - 7, icon_x - 8, icon_y + 7, COLOR2D(255, 0, 0));
    }

    // --- Y BUTTON ---
    // Draw a white button with dark border for better contrast
    m_Graphics->DrawRect(185, 215, 18, 20, COLOR2D(255, 255, 255));
    m_Graphics->DrawRectOutline(185, 215, 18, 20, COLOR2D(0, 0, 0));

    // Draw letter "Y" using lines instead of text
    unsigned y_x = 194;  // Center of Y
    unsigned y_y = 225;  // Center of button

    // Draw Y using thick lines (3px wide)
    // Upper left diagonal of Y
    m_Graphics->DrawLine(y_x - 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 5, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 3, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));

    // Upper right diagonal of Y
    m_Graphics->DrawLine(y_x + 4, y_y - 6, y_x, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 5, y_y - 6, y_x + 1, y_y, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 3, y_y - 6, y_x - 1, y_y, COLOR2D(0, 0, 0));

    // Stem of Y
    m_Graphics->DrawLine(y_x, y_y, y_x, y_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x - 1, y_y, y_x - 1, y_y + 6, COLOR2D(0, 0, 0));
    m_Graphics->DrawLine(y_x + 1, y_y, y_x + 1, y_y + 6, COLOR2D(0, 0, 0));

    // Icon next to Y button - different based on screen type
    unsigned y_icon_x = 215;
    unsigned y_icon_y = 225;

    if (strcmp(screenType, "main") == 0) {
        // Folder icon for main screen
        m_Graphics->DrawRect(y_icon_x, y_icon_y - 2, 16, 11, COLOR2D(255, 255, 255));
        m_Graphics->DrawRect(y_icon_x + 2, y_icon_y - 5, 8, 4, COLOR2D(255, 255, 255));
    } else {
        // GREEN CHECKMARK for all other screens
        // Draw a green checkmark
        // Shorter part of checkmark
        m_Graphics->DrawLine(y_icon_x - 8, y_icon_y, y_icon_x - 3, y_icon_y + 5, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 8, y_icon_y + 1, y_icon_x - 3, y_icon_y + 6, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 7, y_icon_y, y_icon_x - 2, y_icon_y + 5, COLOR2D(0, 255, 0));

        // Longer part of checkmark
        m_Graphics->DrawLine(y_icon_x - 3, y_icon_y + 5, y_icon_x + 8, y_icon_y - 6, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 3, y_icon_y + 6, y_icon_x + 8, y_icon_y - 5, COLOR2D(0, 255, 0));
        m_Graphics->DrawLine(y_icon_x - 2, y_icon_y + 5, y_icon_x + 7, y_icon_y - 4, COLOR2D(0, 255, 0));
    }
}
