/*
/ (c)2025 Ian Cass
/ This class holds the pages in the GUI and manages their
/ transitions
*/

#include "pagemanager.h"

#include <circle/logger.h>
#include <circle/timer.h>
#include <string.h>

LOGMODULE("pagemanager");

PageManager::PageManager() {
}

// Destructor
PageManager::~PageManager() {
    for (int i = 0; i < pageCount; ++i) {
        delete pages[i].page;
        pages[i].page = nullptr;
    }
    pageCount = 0;
}

// Register pages in the GUI. This is called from the
// display implementation (e.g. st7789/display.cpp)
void PageManager::RegisterPage(const char* name, IPage* page) {
    if (pageCount >= MAX_PAGES)
        return;

    pages[pageCount++] = {name, page};
}

// This method and the one below are responsible for
// transitioning to a new page, calling the OnExit and
// OnEnter methods to cleanly transition
void PageManager::SetActivePage(IPage* page) {
    if (currentPage)
        currentPage->OnExit();
    currentPage = page;
    if (currentPage)
        currentPage->OnEnter();
}

void PageManager::SetActivePage(const char* name) {
    IPage* page = GetPage(name);
    if (page)
        SetActivePage(page);
}

IPage* PageManager::GetPage(const char* name) {
    for (int i = 0; i < pageCount; ++i) {
        if (strcmp(pages[i].name, name) == 0)
            return pages[i].page;
    }
    return nullptr;
}

// This is called on a regular basis from the displayservice
// run loop. It uses an observer model to check if the page
// requires transitioning and also calls it refresh method
// to manage screen updates
void PageManager::Refresh(bool redraw) {
    if (currentPage->shouldChangePage()) {
        const char* next = currentPage->nextPageName();
        if (next && strlen(next) > 0) {
            SetActivePage(next);
        }
    } else {
	if (redraw) {
	    currentPage->Draw();
	} else {
            currentPage->Refresh();
        }
    }
}

// This is called from the GPIO interrupt to pass on button
// presses to the page for handling in a page specific manner
void PageManager::HandleButtonPress(Button button) {
    if (currentPage)
        currentPage->OnButtonPress(button);
}
