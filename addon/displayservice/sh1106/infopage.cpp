#include "infopage.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <gitinfo/gitinfo.h>
#include <scsitbservice/scsitbservice.h>
#include <shutdown/shutdown.h>

LOGMODULE("infopage");

SH1106InfoPage::SH1106InfoPage(CSH1106Display* display, C2DGraphics* graphics)
    : m_Display(display),
      m_Graphics(graphics) {
}

SH1106InfoPage::~SH1106InfoPage() {
    LOGNOTE("InfoPage starting");
}

void SH1106InfoPage::OnEnter() {
    LOGNOTE("Drawing InfoPage");
    Draw();
}

void SH1106InfoPage::OnExit() {
    m_ShouldChangePage = false;
}

bool SH1106InfoPage::shouldChangePage() {
    return m_ShouldChangePage;
}

const char* SH1106InfoPage::nextPageName() {
    return "homepage";
}

void SH1106InfoPage::OnButtonPress(Button button) {
    LOGNOTE("Button received by page %d", button);

    switch (button) {
        case Button::Down:
        case Button::Up:
        case Button::Left:
        case Button::Right:
        case Button::Key3:
        case Button::Center:
        case Button::Ok:
        case Button::Cancel:
            LOGNOTE("OK/Cancel");
            m_ShouldChangePage = true;
            break;

        default:
            break;
    }
}

void SH1106InfoPage::MoveSelection(int delta) {
}

void SH1106InfoPage::Refresh() {
}

void SH1106InfoPage::Draw() {
	
    // Create clean version string without "USBODE v" prefix
    char pVersionInfo[32];
    snprintf(pVersionInfo, sizeof(pVersionInfo), "%s.%s.%s",
             CGitInfo::Get()->GetMajorVersion(),
             CGitInfo::Get()->GetMinorVersion(),
             CGitInfo::Get()->GetPatchVersion());

    //const char* pBuildNumber = CGitInfo::Get()->GetBuildNumber();
    //const char* pBuildDate = __DATE__ " " __TIME__;
    const char* pGitBranch = GIT_BRANCH;
    //const char* pGitCommit = GIT_COMMIT;

    m_Graphics->ClearScreen(COLOR2D(0, 0, 0));
    m_Graphics->DrawRect(0, 0, m_Display->GetWidth(), 10, COLOR2D(255, 255, 255));
    m_Graphics->DrawText(2, 1, COLOR2D(0, 0, 0), "Build Info", C2DGraphics::AlignLeft, Font8x8);



    // Create a comprehensive build info string using the full version string and branch info
    char buildInfo[512];
    snprintf(buildInfo, sizeof(buildInfo),
	     "%s %s%s",
	     CGitInfo::Get()->GetFullVersionString(),
	     pGitBranch,
	     (strcmp(pGitBranch, "main") == 0) ? " *" : "");

    // Display the complete build info with word wrapping
    const size_t chars_per_line = 21; // Maximum chars per line for SH1106

    size_t total_length = strlen(buildInfo);
    size_t current_pos = 0;
    unsigned int y_pos = 16; // Start position for text

    // Word wrapping implementation
    while (current_pos < total_length && y_pos < 55) {
	// Determine how many characters fit on this line
	size_t chars_to_display = chars_per_line;

	// Adjust for end of string
	if (current_pos + chars_to_display > total_length) {
	    chars_to_display = total_length - current_pos;
	}
	// Try to break at word boundaries
	else if (current_pos + chars_to_display < total_length) {
	    // Find the last space in the line
	    size_t space_pos = chars_to_display;
	    while (space_pos > 0 && buildInfo[current_pos + space_pos] != ' ') {
		space_pos--;
	    }

	    // If we found a space, break there
	    if (space_pos > 0) {
		chars_to_display = space_pos;
	    }
	}

	// Copy this line's text
	char line[32] = {0};
	strncpy(line, buildInfo + current_pos, chars_to_display);
	line[chars_to_display] = '\0';

	// Draw this line
        m_Graphics->DrawText(0, y_pos, COLOR2D(255,255,255), line, C2DGraphics::AlignLeft, Font6x7);

	// Move to next line
	current_pos += chars_to_display;

	// Skip space at beginning of next line
	if (current_pos < total_length && buildInfo[current_pos] == ' ') {
	    current_pos++;
	}

	y_pos += 10; // Line spacing
    }

    // Draw a "Back" instruction at the bottom
    m_Graphics->DrawText(0, 56, COLOR2D(255,255,255), "Press any key...", C2DGraphics::AlignLeft, Font6x7);

    m_Graphics->UpdateDisplay();
}
