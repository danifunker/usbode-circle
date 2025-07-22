#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

// This class is a holder for useful objects
// It gets passed to the GPIO interrupt handler
// and is used during button press callbacks
struct ButtonHandlerContext {
    IDisplay* display;
    PageManager* pageManager;
    CGPIOPin* pin;
    Button button;
};

#endif

