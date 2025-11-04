#ifndef _CUEBINFILETYTPES_H
#define _CUEBINFILETYTPES_H

enum class FileType {
    ISO,
    CUEBIN
};

enum class MEDIA_TYPE {
    NONE,
    CD,
    DVD
};

enum class DISC_TYPE
{
    UNKNOWN,
    CDDA,      // Pure audio CD
    CDROM,     // Pure data CD
    MIXED_MODE, // Mixed audio/data
    DVDDISC
};

#endif
