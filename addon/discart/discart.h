#ifndef DISCART_H
#define DISCART_H

#include <circle/types.h>

#define DISCART_WIDTH  240
#define DISCART_HEIGHT 240
#define DISCART_BUFFER_SIZE (DISCART_WIDTH * DISCART_HEIGHT * sizeof(u16))

class DiscArt {
public:
    // Get the disc art path for a given disc image path
    // Returns true if disc art exists, false otherwise
    // artPath must be at least 512 bytes
    static bool GetDiscArtPath(const char* discImagePath, char* artPath, size_t artPathSize);

    // Check if disc art exists for a given disc image path
    static bool HasDiscArt(const char* discImagePath);

    // Load disc art into RGB565 buffer for display
    // buffer must be at least DISCART_BUFFER_SIZE bytes
    // Returns true on success, false on failure
    static bool LoadDiscArtRGB565(const char* discImagePath, u16* buffer);

    // Get file size of disc art (for web serving)
    static unsigned int GetDiscArtFileSize(const char* discImagePath);

    // Read disc art file contents (for web serving)
    // Returns bytes read, 0 on failure
    static unsigned int ReadDiscArtFile(const char* discImagePath, u8* buffer, unsigned int bufferSize);

private:
    static bool FileExists(const char* path);
    static void StripExtension(const char* path, char* result, size_t resultSize);
};

#endif // DISCART_H
