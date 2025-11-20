// util.h
#ifndef UTIL_H
#define UTIL_H
#include <circle/util.h>
#include "imagedevice.h" 
#include "cuebinfile.h"

#define MAX_FILENAME 255

// File extension helpers
char tolower(char c);
bool hasBinExtension(const char* imageName);
bool hasMdsExtension(const char* imageName);
bool hasIsoExtension(const char* imageName);
bool hasCueExtension(const char* imageName);
bool hasChdExtension(const char* imageName);
void change_extension_to_cue(char* fullPath);
void change_extension_to_bin(char* fullPath);
bool hasDvdHint(const char* imageName);

// Image loading - returns base IImageDevice interface
IImageDevice* loadImageDevice(const char* imageName);

// Format-specific loaders
IImageDevice* loadMDSFileDevice(const char* imageName);
IImageDevice* loadCueBinIsoFileDevice(const char* imageName);
IImageDevice* loadCHDFileDevice(const char* imageName);
// Legacy compatibility function
// TODO: Remove once all code migrated to IImageDevice
inline ICueDevice* loadCueBinFileDevice(const char* imageName) {
    IImageDevice* device = loadImageDevice(imageName);
    // Only safe if we know it's a CUE device
    // For MDS files, this will return nullptr
    if (device && device->GetFileType() == FileType::CUEBIN) {
        return static_cast<ICueDevice*>(device);
    }
    if (device && device->GetFileType() == FileType::ISO) {
        return static_cast<ICueDevice*>(device);
    }
    return nullptr;
}

#endif  // UTIL_H