// util.h
#ifndef URLDECODER_H
#define URLDECODER_H
#include <circle/util.h>
#include <discimage/cuebinfile.h>

#define MAX_FILENAME 255

bool is_hex_digit(char c);
void urldecode(char* dst, const char* src);

#endif  // UTIL_H
