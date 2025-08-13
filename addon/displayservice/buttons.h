#ifndef BUTTONS_H
#define BUTTONS_H

// An enum representing the buttons
enum class Button
{
    Up,
    Down,
    Left,
    Right,
    Center,
    Ok,
    Cancel,
    Key3,
    Count // sentinal value - keep this as the last entry!!!
};

struct ButtonConfig {
    unsigned Up = 0;
    unsigned Down = 0;
    unsigned Left = 0;
    unsigned Right = 0;
    unsigned Ok = 0;
    unsigned Cancel = 0;
    unsigned Key3 = 0; // Additional button, if available
    unsigned Center = 0;
};

#endif
