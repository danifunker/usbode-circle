#ifndef BUTTONS_H
#define BUTTONS_H

// An enum representing the buttons
enum class Button
{
    Up,
    Down,
    Left,
    Right,
    Cancel,
    Ok,
    Count // sentinal value - keep this as the last entry!!!
};

struct ButtonConfig {
    unsigned Up = 0;
    unsigned Down = 0;
    unsigned Left = 0;
    unsigned Right = 0;
    unsigned Ok = 0;
    unsigned Cancel = 0;
};

#endif
