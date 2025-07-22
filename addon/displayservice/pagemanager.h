#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include <circle/gpiopin.h>
#include <circle/types.h>
#include <displayservice/buttons.h>

#include "idisplay.h"
#include "ipage.h"

class PageManager {
   public:
    PageManager();
    ~PageManager();

    void RegisterPage(const char* name, IPage* page);
    void SetActivePage(IPage* page);
    void SetActivePage(const char* name);
    void Refresh();

    IPage* GetPage(const char* name);

    void HandleButtonPress(Button button);

   private:
    struct PageEntry {
        const char* name;
        IPage* page;
    };

    static constexpr int MAX_PAGES = 10;
    PageEntry pages[MAX_PAGES];
    int pageCount = 0;

    IPage* currentPage = nullptr;
};

#endif
