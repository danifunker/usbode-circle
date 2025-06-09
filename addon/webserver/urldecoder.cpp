// util.cpp
#include "urldecoder.h"

LOGMODULE("util");

// // Check if a character is a hexadecimal digit (0-9, A-F, a-f)
bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

// URL decode a string
void urldecode(char* dst, const char* src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            (is_hex_digit(a) && is_hex_digit(b))) {
            if (a >= 'a')
                a -= ('a' - 10);
            else if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';

            if (b >= 'a')
                b -= ('a' - 10);
            else if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';

            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
