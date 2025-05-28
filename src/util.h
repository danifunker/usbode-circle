// util.h
#ifndef UTIL_H
#define UTIL_H
#include <circle/util.h>
#include <discimage/cuebinfile.h>

#define MAX_FILENAME 255

char tolower(char c);
bool hasBinExtension(const char* imageName);
void change_extension_to_cue(char* fullPath);
CCueBinFileDevice* loadCueBinFileDevice(const char* imageName);
bool is_hex_digit(char c);
void urldecode(char* dst, const char* src);

#endif  // UTIL_H
