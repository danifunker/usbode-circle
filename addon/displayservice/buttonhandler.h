#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

struct ButtonHandlerContext {
    IDisplay* display;
    PageManager* pageManager;
    CGPIOPin* pin;
    Button button;
};

#endif

