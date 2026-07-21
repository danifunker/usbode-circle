//
// fakedisc.cpp
//
#include "fakedisc.h"

#include <stdio.h>

void FillPatternSector(u8 *dest, u32 lba, u32 sectorSize)
{
    for (u32 j = 0; j < sectorSize; j++)
    {
        dest[j] = (u8)((lba * 7 + j) & 0xFF);
    }
    dest[0] = (u8)(lba >> 24);
    dest[1] = (u8)(lba >> 16);
    dest[2] = (u8)(lba >> 8);
    dest[3] = (u8)(lba >> 0);
}

static std::string MSFString(u32 lba)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", lba / (75 * 60), (lba / 75) % 60, lba % 75);
    return buf;
}

static std::vector<u8> MakePatternImage(u32 numSectors, u32 sectorSize)
{
    std::vector<u8> image((size_t)numSectors * sectorSize);
    for (u32 lba = 0; lba < numSectors; lba++)
    {
        FillPatternSector(image.data() + (size_t)lba * sectorSize, lba, sectorSize);
    }
    return image;
}

CFakeImageDevice *MakeDataISO(u32 numSectors)
{
    std::string cue = "FILE \"image.bin\" BINARY\n"
                      "  TRACK 01 MODE1/2048\n"
                      "    INDEX 01 00:00:00\n";

    CFakeImageDevice *dev = new CFakeImageDevice(cue, MakePatternImage(numSectors, 2048), 2048);
    dev->m_numTracks = 1;
    return dev;
}

CFakeImageDevice *MakeAudioCD(int nTracks, u32 sectorsPerTrack)
{
    std::string cue = "FILE \"image.bin\" BINARY\n";
    for (int t = 0; t < nTracks; t++)
    {
        char line[64];
        snprintf(line, sizeof(line), "  TRACK %02d AUDIO\n", t + 1);
        cue += line;
        cue += "    INDEX 01 " + MSFString((u32)t * sectorsPerTrack) + "\n";
    }

    CFakeImageDevice *dev = new CFakeImageDevice(
        cue, MakePatternImage((u32)nTracks * sectorsPerTrack, 2352), 2352);
    dev->m_numTracks = nTracks;
    return dev;
}

CFakeImageDevice *MakeMixedModeCD(u32 dataSectors, int nAudioTracks, u32 audioSectorsPerTrack)
{
    std::string cue = "FILE \"image.bin\" BINARY\n"
                      "  TRACK 01 MODE1/2352\n"
                      "    INDEX 01 00:00:00\n";
    for (int t = 0; t < nAudioTracks; t++)
    {
        char line[64];
        snprintf(line, sizeof(line), "  TRACK %02d AUDIO\n", t + 2);
        cue += line;
        cue += "    INDEX 01 " + MSFString(dataSectors + (u32)t * audioSectorsPerTrack) + "\n";
    }

    u32 total = dataSectors + (u32)nAudioTracks * audioSectorsPerTrack;
    CFakeImageDevice *dev = new CFakeImageDevice(cue, MakePatternImage(total, 2352), 2352);
    dev->m_numTracks = 1 + nAudioTracks;
    return dev;
}
