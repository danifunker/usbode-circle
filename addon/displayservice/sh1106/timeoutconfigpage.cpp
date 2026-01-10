#include "timeoutconfigpage.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>
#include <shutdown/shutdown.h>
#include <linux/kernel.h>

LOGMODULE("timeoutconfigpage");

SH1106TimeoutConfigPage::SH1106TimeoutConfigPage(CSH1106Display* display, C2DGraphics* graphics)
: m_Display(display),
  m_Graphics(graphics)
{
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
}

SH1106TimeoutConfigPage::~SH1106TimeoutConfigPage() {
    LOGNOTE("TimeoutConfigPage starting");
}

void SH1106TimeoutConfigPage::OnEnter()
{
    LOGNOTE("Drawing TimeoutConfigPage");

    unsigned currentTimeout = configservice->GetScreenTimeout();
    
    // Check if current timeout matches any predefined option
    bool foundMatch = false;
    for (size_t i = 0; i < 7; ++i) {
        if (timeoutValues[i] == currentTimeout) {
            foundMatch = true;
            m_SelectedIndex = i;
            m_OptionCount = 7;
            break;
        }
    }
    
    // If no match, add custom option
    if (!foundMatch) {
        if (currentTimeout >= 60) {
            snprintf(customLabel, sizeof(customLabel), "Custom: %u min", currentTimeout / 60);
        } else {
            snprintf(customLabel, sizeof(customLabel), "Custom: %us", currentTimeout);
        }
        
        // Find insertion point to keep sorted order
        size_t insertIdx = 0;
        for (size_t i = 0; i < 7; ++i) {
            if (currentTimeout == 0 || (timeoutValues[i] != 0 && currentTimeout > timeoutValues[i])) {
                insertIdx = i + 1;
            }
        }
        
        // Shift options to make room
        for (size_t i = 7; i > insertIdx; --i) {
            options[i] = options[i - 1];
            timeoutValues[i] = timeoutValues[i - 1];
        }
        
        // Insert custom option
        options[insertIdx] = customLabel;
        timeoutValues[insertIdx] = currentTimeout;
        m_SelectedIndex = insertIdx;
        m_OptionCount = 8;
    }

    Draw();
}

size_t SH1106TimeoutConfigPage::FindClosestTimeout(unsigned currentTimeout)
{
    size_t closestIndex = 0;
    unsigned minDiff = 0xFFFFFFFF;  // Max unsigned value

    for (size_t i = 0; i < 7; ++i) {
        unsigned diff = (currentTimeout > timeoutValues[i]) 
            ? (currentTimeout - timeoutValues[i]) 
            : (timeoutValues[i] - currentTimeout);
        
        if (diff < minDiff) {
            minDiff = diff;
            closestIndex = i;
        }
    }

    return closestIndex;
}

void SH1106TimeoutConfigPage::OnExit()
{
	m_ShouldChangePage = false;
}

bool SH1106TimeoutConfigPage::shouldChangePage() {
	return m_ShouldChangePage;
}

const char* SH1106TimeoutConfigPage::nextPageName() {
	return m_NextPageName;
}

void SH1106TimeoutConfigPage::OnButtonPress(Button button)
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
        case Button::Center:
	{
	    unsigned timeout = timeoutValues[m_SelectedIndex];
	    LOGNOTE("Setting screen timeout to %d", timeout);
	    configservice->SetScreenTimeout(timeout);
	    m_NextPageName = "homepage";
	    m_ShouldChangePage = true;
            break;
	}

        case Button::Cancel:
	    LOGNOTE("Cancel");
	    m_NextPageName = "configpage";
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
    
}

void SH1106TimeoutConfigPage::MoveSelection(int delta) {

    if (m_OptionCount == 0) return;

    LOGDBG("Selected index is %d, Menu delta is %d", m_SelectedIndex, delta);
    int newIndex = static_cast<int>(m_SelectedIndex) + delta;
    if (newIndex < 0)
        newIndex = 0;
    else if (newIndex >= static_cast<int>(m_OptionCount))
        newIndex = static_cast<int>(m_OptionCount - 1);

    if (static_cast<size_t>(newIndex) != m_SelectedIndex) {
	LOGDBG("New menu index is %d", newIndex);
        m_SelectedIndex = static_cast<size_t>(newIndex);
        Draw();
    }
}

void SH1106TimeoutConfigPage::Refresh()
{
}

void SH1106TimeoutConfigPage::Draw()
{

    if (m_OptionCount == 0) return;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "TimeoutConfig", C2DGraphics::AlignLeft, Font8x8);

    size_t startIndex = 0;
    size_t endIndex = m_OptionCount;

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

