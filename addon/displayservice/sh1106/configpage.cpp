#include "configpage.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>

LOGMODULE("configpage");

SH1106ConfigPage::SH1106ConfigPage(CSH1106Display* display, C2DGraphics* graphics)
: m_Display(display),
  m_Graphics(graphics)
{
}

SH1106ConfigPage::~SH1106ConfigPage() {
    LOGNOTE("ConfigPage starting");
}

void SH1106ConfigPage::OnEnter()
{
    LOGNOTE("Drawing ConfigPage");

    Draw();
}

void SH1106ConfigPage::OnExit()
{
	m_ShouldChangePage = false;
}

bool SH1106ConfigPage::shouldChangePage() {
	return m_ShouldChangePage;
}

const char* SH1106ConfigPage::nextPageName() {
	return m_NextPageName;
}

void SH1106ConfigPage::OnButtonPress(Button button)
{
	LOGNOTE("Button received by page %d", button);
	
    switch (button)
    {
        case Button::Up:
	    LOGNOTE("Move Up");
            MoveSelection(-1);
            break;

        case Button::Down:
	    LOGNOTE("Move Down");
            MoveSelection(+1);
            break;

        case Button::Ok:
	    //TODO show an acknowledgement screen rather then just doing
	    switch (m_SelectedIndex) {
		    case 0:
	                    LOGNOTE("USB Configuration");
			    m_NextPageName = "usbconfigpage";
			    m_ShouldChangePage = true;
			    break;
		    case 1:
			    LOGNOTE("Logging Configuration");
			    m_NextPageName = "logconfigpage";
			    m_ShouldChangePage = true;
			    break;
	    }
            break;

        case Button::Cancel:
	    LOGNOTE("Cancel");
	    m_NextPageName = "homepage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
    
}

void SH1106ConfigPage::MoveSelection(int delta) {

    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    LOGDBG("Selected index is %d, Menu delta is %d", m_SelectedIndex, delta);
    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = 0;
    else if (newIndex >= static_cast<int>(fileCount))
        newIndex = static_cast<int>(fileCount - 1);

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
	LOGDBG("New menu index is %d", newIndex);
        m_SelectedIndex = static_cast<size_t>(newIndex);
        Draw();
    }
}

void SH1106ConfigPage::Refresh()
{
}

void SH1106ConfigPage::Draw()
{

    size_t fileCount = sizeof(options) / sizeof(options[0]);
    if (fileCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Config", C2DGraphics::AlignLeft, Font8x8);

    size_t startIndex = 0;
    size_t endIndex = fileCount;

    for (size_t i = startIndex; i < endIndex; ++i) {
        int y = static_cast<int>((i - startIndex) * 10);
        const char* name = options[i];

        if (i == m_SelectedIndex) {
            m_Graphics->DrawRect(0, y + 15, m_Display->GetWidth(), 9, COLOR2D(255, 255, 255));
            m_Graphics->DrawText(0, y + 16, COLOR2D(0,0,0), name, C2DGraphics::AlignLeft, Font6x7);
        } else {
            m_Graphics->DrawText(0, y + 16, COLOR2D(255,255,255), name, C2DGraphics::AlignLeft, Font6x7);
        }
    }
    m_Graphics->UpdateDisplay();
}

