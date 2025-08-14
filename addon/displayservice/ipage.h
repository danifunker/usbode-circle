#ifndef IPAGE_H
#define IPAGE_H

#include <displayservice/buttons.h>

// This interface defines a page and is implemented by
// all pages in our GUI
class IPage {
   public:
    virtual void OnEnter() = 0;
    virtual void OnExit() = 0;
    virtual void OnButtonPress(Button buttonId) = 0;
    virtual void Refresh() = 0;
    virtual void Draw() = 0;
    virtual bool shouldChangePage() = 0;
    virtual const char* nextPageName() = 0;
    virtual ~IPage() {}
};

#endif
