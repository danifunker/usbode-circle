#include "imagespage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>

LOGMODULE("imagespage");

SH1106ImagesPage::SH1106ImagesPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
    m_Service = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    // Initialize some variables
    CCharGenerator Font(Font6x7, CCharGenerator::FontFlagsNone);
    charWidth = Font.GetCharWidth();
    maxTextPx = m_Display->GetWidth() - 10;
}

SH1106ImagesPage::~SH1106ImagesPage() {
    LOGNOTE("Imagespage starting");
}

void SH1106ImagesPage::OnEnter() {
    LOGNOTE("Drawing imagespage");
    const char* name = m_Service->GetCurrentCDName();
    SetSelectedName(name);
    m_MountedIndex = m_SelectedIndex;
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

void SH1106ImagesPage::DrawText(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
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

void SH1106ImagesPage::DrawTextScrolled(unsigned nX, unsigned nY, T2DColor Color, const char* pText,
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

void SH1106ImagesPage::RefreshScroll() {
    const char* name = m_Service->GetName(m_SelectedIndex);
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

    size_t fileCount = m_Service->GetCount();
    if (fileCount == 0) return;

    dirty = false;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Images", C2DGraphics::AlignLeft, Font8x8);

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
        int y = static_cast<int>((i - startIndex) * 10);
        const char* name = m_Service->GetName(i);
        size_t nameLen = strlen(name);
        char extended[nameLen + 2];
        snprintf(extended, sizeof(extended), "%s ", name);

        // Crop
        const int maxLen = maxTextPx / charWidth;
        char cropped[maxLen + 2];
        snprintf(cropped, sizeof(cropped), "%s%.*s", i == m_MountedIndex?"*":"", maxLen, name);

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
            DrawText(0, y + 16, COLOR2D(0,0,0), cropped, Font6x7);
        } else {
            DrawText(0, y + 16, COLOR2D(255, 255, 255), cropped, Font6x7);
        }
    }

    RefreshScroll();

    // Draw page indicator
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", currentPage + 1, totalPages);
    m_Graphics->DrawText(85, 1, COLOR2D(0,0,0), pageText, C2DGraphics::AlignLeft, Font6x7);

    m_Graphics->UpdateDisplay();
}

