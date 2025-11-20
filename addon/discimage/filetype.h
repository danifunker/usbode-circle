#ifndef _CUEBINFILETYTPES_H
#define _CUEBINFILETYTPES_H

enum class FileType {
    UNKNOWN,
    ISO,        // Plain ISO image
    CUEBIN,     // CUE/BIN pair
    MDS,        // MDS/MDF pair (Alcohol 120%)
    CHD,        // MAME Compressed Hunks of Data
    // Future formats:
    // NRG,     // Nero image
    // CDI,     // DiscJuggler
    // CCD,     // CloneCD
};

enum class MEDIA_TYPE {
    NONE,
    CD,
    DVD
};

#endif