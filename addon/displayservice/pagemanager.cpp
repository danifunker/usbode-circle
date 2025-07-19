#include "pagemanager.h"
#include <string.h>
#include <circle/timer.h>
#include <circle/logger.h>

LOGMODULE("pagemanager");

PageManager::PageManager()
{
}

PageManager::~PageManager()
{
    for (unsigned i = 0; i < pageCount; ++i) {
        delete pages[i].page;
        pages[i].page = nullptr;
    }
    pageCount = 0;
}

//TODO should these methods be in a base class for "display"?
void PageManager::RegisterPage(const char* name, IPage* page)
{
    if (pageCount >= MAX_PAGES)
        return;

    pages[pageCount++] = { name, page };
}

void PageManager::SetActivePage(IPage* page)
{
    if (currentPage)
        currentPage->OnExit();
    currentPage = page;
    if (currentPage)
        currentPage->OnEnter();
}

void PageManager::SetActivePage(const char* name)
{
    IPage* page = GetPage(name);
    if (page)
        SetActivePage(page);
}

IPage* PageManager::GetPage(const char* name)
{
    for (int i = 0; i < pageCount; ++i)
    {
        if (strcmp(pages[i].name, name) == 0)
            return pages[i].page;
    }
    return nullptr;
}

void PageManager::Refresh() 
{
	if (currentPage->shouldChangePage()) {
	    const char* next = currentPage->nextPageName();
            if (next && strlen(next) > 0)
            {
                SetActivePage(next);
            }
	} else {
		currentPage->Refresh();
	}
}

void PageManager::HandleButtonPress(Button button)
{
    //TODO make this configurable
    constexpr unsigned debounceTicks = 20;
    unsigned now = CTimer::Get()->GetTicks();
    if (now - lastPressTime[(int)button] < debounceTicks) {
	LOGNOTE("Ignored a bounce!");
        return;
    }

    lastPressTime[(int)button] = now;

    if (currentPage)
        currentPage->OnButtonPress(button);
}
