#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cueparser/cueparser.h>

// CHD metadata tags
#define CDROM_TRACK_METADATA_TAG   'TRACK'
#define CDROM_METADATA_CODEC_MASK  0xf0000000
#define CHD_CODEC_NONE             0x00000000
#define CHD_CODEC_ZLIB             0x10000000
#define CHD_CODEC_LZMA             0x20000000
#define CHD_CODEC_HUFFMAN          0x30000000
#define CHD_CODEC_FLAC             0x40000000
#define CHD_CODEC_ZSTD             0x50000000

class CHDParser {
public:
    CHDParser();
    ~CHDParser();
    
    // Initialize with a file handle to the CHD file
    bool initialize(void* fileHandle);
    
    // Generate a CUE sheet from CHD metadata
    const char* generateCueSheet();
    
    // Get the raw sector data for a given LBA
    bool readSector(uint32_t lba, void* buffer, uint32_t sectorSize);
    
    // Get track info similar to CUE parser
    const CUETrackInfo* getTrackInfo(int trackNumber);
    
    // Get number of tracks
    int getNumTracks() const;
    
private:
    void* m_fileHandle;
    char* m_cueSheet;
    int m_numTracks;
    CUETrackInfo* m_trackInfo;
    
    // Parse CHD header and metadata
    bool parseHeader();
    bool parseMetadata();
    
    // Helper functions for decompression
    bool decompressSector(uint32_t hunkNumber, void* buffer);
};