#ifndef IPAGE_H
#define IPAGE_H

#include <displayservice/buttons.h>

class IPage {
public:
    virtual void OnEnter() = 0;                      // Called when screen becomes active
    virtual void OnExit() = 0;                      // Called when screen becomes inactive
    virtual void OnButtonPress(Button buttonId) = 0;    // Handle button input
    virtual void Refresh() = 0;                       // Called from main loop to redraw or refresh
    virtual bool shouldChangePage() = 0;
    virtual const char* nextPageName() = 0;
    virtual ~IPage() {}
};

#endif
