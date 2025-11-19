// util.h
#ifndef UTIL_H
#define UTIL_H
#include <circle/util.h>
#include "imagedevice.h"
#include "cuebinfile.h"

#define MAX_FILENAME 255


char tolower(char c);
bool hasBinExtension(const char* imageName);
void change_extension_to_cue(char* fullPath);
IImageDevice* loadImageDevice(const char* imageName);
bool hasDvdHint(const char* imageName);
bool ReadFileToString(const char* fullPath, char** out_str);
#endif  // UTIL_H
