/*
 * Disc Art - JPEG disc art loading for USBODE
 * Provides disc art path resolution and JPEG decoding for display
 */

#include "discart.h"
#include "tjpgd.h"

#include <circle/logger.h>
#include <fatfs/ff.h>
#include <string.h>
#include <cstdio>
#include <ctype.h>

LOGMODULE("discart");

// JPEG decoder work buffer size (needs to be large enough for decoder state)
#define JPEG_WORK_BUFFER_SIZE 4096

// Structure passed to JPEG decoder callbacks
struct JpegDecodeContext {
    FIL* file;
    u16* outputBuffer;
    unsigned int outputWidth;
    unsigned int outputHeight;
};

// JPEG input callback - reads data from FatFS file
static size_t jpeg_input_func(JDEC* jd, uint8_t* buff, size_t nbyte) {
    JpegDecodeContext* ctx = (JpegDecodeContext*)jd->device;
    UINT bytesRead = 0;

    if (buff) {
        // Read data into buffer
        FRESULT res = f_read(ctx->file, buff, nbyte, &bytesRead);
        if (res != FR_OK) {
            LOGERR("JPEG read error: %d", res);
            return 0;
        }
    } else {
        // Skip data (seek forward)
        FRESULT res = f_lseek(ctx->file, f_tell(ctx->file) + nbyte);
        if (res != FR_OK) {
            LOGERR("JPEG seek error: %d", res);
            return 0;
        }
        bytesRead = nbyte;
    }

    return bytesRead;
}

// JPEG output callback - writes decoded RGB565 pixels to output buffer
static int jpeg_output_func(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegDecodeContext* ctx = (JpegDecodeContext*)jd->device;
    u16* src = (u16*)bitmap;

    // Calculate output position
    unsigned int width = rect->right - rect->left + 1;
    unsigned int height = rect->bottom - rect->top + 1;

    // Copy pixels to output buffer, clipping to output dimensions
    for (unsigned int y = 0; y < height; y++) {
        unsigned int outY = rect->top + y;
        if (outY >= ctx->outputHeight) continue;

        for (unsigned int x = 0; x < width; x++) {
            unsigned int outX = rect->left + x;
            if (outX >= ctx->outputWidth) continue;

            unsigned int outIdx = outY * ctx->outputWidth + outX;
            ctx->outputBuffer[outIdx] = src[y * width + x];
        }
    }

    return 1; // Continue decompression
}

bool DiscArt::FileExists(const char* path) {
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK;
}

void DiscArt::StripExtension(const char* path, char* result, size_t resultSize) {
    strncpy(result, path, resultSize - 1);
    result[resultSize - 1] = '\0';

    // Find last dot after last slash
    char* lastSlash = strrchr(result, '/');
    char* lastDot = strrchr(result, '.');

    if (lastDot && (!lastSlash || lastDot > lastSlash)) {
        *lastDot = '\0';
    }
}

bool DiscArt::GetDiscArtPath(const char* discImagePath, char* artPath, size_t artPathSize) {
    if (!discImagePath || !artPath || artPathSize < 10) {
        return false;
    }

    // Strip extension from disc image path
    char basePath[512];
    StripExtension(discImagePath, basePath, sizeof(basePath));

    // Try .jpg extension
    snprintf(artPath, artPathSize, "%s.jpg", basePath);
    if (FileExists(artPath)) {
        LOGNOTE("Found disc art: %s", artPath);
        return true;
    }

    // Try .jpeg extension
    snprintf(artPath, artPathSize, "%s.jpeg", basePath);
    if (FileExists(artPath)) {
        LOGNOTE("Found disc art: %s", artPath);
        return true;
    }

    // If disc image is .bin, also check for .cue basename
    const char* ext = strrchr(discImagePath, '.');
    // Case-insensitive comparison for .bin
    bool isBin = ext && (strcmp(ext, ".bin") == 0 || strcmp(ext, ".BIN") == 0 ||
                         strcmp(ext, ".Bin") == 0);
    if (isBin) {
        // Try .cue basename
        snprintf(artPath, artPathSize, "%s.cue", basePath);
        char cuePath[512];
        strncpy(cuePath, artPath, sizeof(cuePath) - 1);
        cuePath[sizeof(cuePath) - 1] = '\0';

        // Check if .cue file exists, then look for art with that base
        if (FileExists(cuePath)) {
            StripExtension(cuePath, basePath, sizeof(basePath));
            snprintf(artPath, artPathSize, "%s.jpg", basePath);
            if (FileExists(artPath)) {
                LOGNOTE("Found disc art via .cue: %s", artPath);
                return true;
            }
        }
    }

    artPath[0] = '\0';
    return false;
}

bool DiscArt::HasDiscArt(const char* discImagePath) {
    char artPath[512];
    return GetDiscArtPath(discImagePath, artPath, sizeof(artPath));
}

bool DiscArt::LoadDiscArtRGB565(const char* discImagePath, u16* buffer) {
    if (!discImagePath || !buffer) {
        return false;
    }

    // Get disc art path
    char artPath[512];
    if (!GetDiscArtPath(discImagePath, artPath, sizeof(artPath))) {
        LOGNOTE("No disc art found for: %s", discImagePath);
        return false;
    }

    // Open the JPEG file
    FIL file;
    FRESULT res = f_open(&file, artPath, FA_READ);
    if (res != FR_OK) {
        LOGERR("Failed to open disc art file: %s (error %d)", artPath, res);
        return false;
    }

    // Allocate work buffer for JPEG decoder
    void* workBuffer = new u8[JPEG_WORK_BUFFER_SIZE];
    if (!workBuffer) {
        LOGERR("Failed to allocate JPEG work buffer");
        f_close(&file);
        return false;
    }

    // Set up decode context
    JpegDecodeContext ctx;
    ctx.file = &file;
    ctx.outputBuffer = buffer;
    ctx.outputWidth = DISCART_WIDTH;
    ctx.outputHeight = DISCART_HEIGHT;

    // Clear output buffer (fill with black)
    memset(buffer, 0, DISCART_BUFFER_SIZE);

    // Initialize JPEG decoder
    JDEC jdec;
    JRESULT jres = jd_prepare(&jdec, jpeg_input_func, workBuffer, JPEG_WORK_BUFFER_SIZE, &ctx);
    if (jres != JDR_OK) {
        LOGERR("JPEG prepare failed: %d", jres);
        delete[] (u8*)workBuffer;
        f_close(&file);
        return false;
    }

    LOGNOTE("JPEG image: %dx%d", jdec.width, jdec.height);

    // Check if image fits
    if (jdec.width > DISCART_WIDTH || jdec.height > DISCART_HEIGHT) {
        LOGWARN("Disc art too large: %dx%d (max %dx%d)",
                jdec.width, jdec.height, DISCART_WIDTH, DISCART_HEIGHT);
        // Continue anyway - will be clipped
    }

    // Decompress the image
    jres = jd_decomp(&jdec, jpeg_output_func, 0);
    if (jres != JDR_OK) {
        LOGERR("JPEG decompress failed: %d", jres);
        delete[] (u8*)workBuffer;
        f_close(&file);
        return false;
    }

    // Clean up
    delete[] (u8*)workBuffer;
    f_close(&file);

    LOGNOTE("Disc art loaded successfully");
    return true;
}

unsigned int DiscArt::GetDiscArtFileSize(const char* discImagePath) {
    char artPath[512];
    if (!GetDiscArtPath(discImagePath, artPath, sizeof(artPath))) {
        return 0;
    }

    FILINFO fno;
    if (f_stat(artPath, &fno) != FR_OK) {
        return 0;
    }

    return fno.fsize;
}

unsigned int DiscArt::ReadDiscArtFile(const char* discImagePath, u8* buffer, unsigned int bufferSize) {
    if (!buffer || bufferSize == 0) {
        return 0;
    }

    char artPath[512];
    if (!GetDiscArtPath(discImagePath, artPath, sizeof(artPath))) {
        return 0;
    }

    FIL file;
    FRESULT res = f_open(&file, artPath, FA_READ);
    if (res != FR_OK) {
        return 0;
    }

    UINT bytesRead = 0;
    res = f_read(&file, buffer, bufferSize, &bytesRead);
    f_close(&file);

    if (res != FR_OK) {
        return 0;
    }

    return bytesRead;
}
