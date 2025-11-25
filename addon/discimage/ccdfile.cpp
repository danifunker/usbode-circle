// addon/discimage/ccdfile.cpp

#include "ccdfile.h"
#include <circle/logger.h>
#include <circle/alloc.h>
#include <circle/util.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>

LOGMODULE("ccdfile");

// Helper functions
static bool beginsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

// Helper to parse INI values
static char* parseValue(const char* line, const char* key) {
    const char* p = strstr(line, key);
    if (!p) return nullptr;

    p += strlen(key);

    // Handle spaces before '='
    while (*p && isspace(*p)) p++;

    // Check for '='
    if (*p != '=') return nullptr;
    p++; // skip '='

    // Skip spaces after '='
    while (*p && isspace(*p)) p++;

    char* value = new char[strlen(p) + 1];
    strcpy(value, p);

    // Trim trailing whitespace/newline/CR
    char* end = value + strlen(value) - 1;
    while (end >= value && (isspace(*end) || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    return value;
}

static int parseInt(const char* line, const char* key) {
    char* val = parseValue(line, key);
    if (!val) return 0;
    int res = strtol(val, nullptr, 0); 
    delete[] val;
    return res;
}

CCcdFileDevice::CCcdFileDevice(const char* imgPath, const char* subPath, const char* ccdContent, MEDIA_TYPE mediaType)
    : m_ImgPath(nullptr), m_SubPath(nullptr), m_CcdContent(ccdContent), m_CueSheet(nullptr), m_CurrentOffset(0), m_MainDataOffset(0), m_SubDataOffset(0), m_MediaType(mediaType) {

    if (imgPath) {
        m_ImgPath = new char[strlen(imgPath) + 1];
        strcpy(m_ImgPath, imgPath);
    }

    if (subPath) {
        m_SubPath = new char[strlen(subPath) + 1];
        strcpy(m_SubPath, subPath);
    }

    m_ImgFile = new FIL();
    m_SubFile = new FIL();
}

CCcdFileDevice::~CCcdFileDevice() {
    if (m_ImgFile) {
        f_close(m_ImgFile);
        delete m_ImgFile;
    }
    if (m_SubFile) {
        f_close(m_SubFile);
        delete m_SubFile;
    }
    if (m_ImgPath) delete[] m_ImgPath;
    if (m_SubPath) delete[] m_SubPath;
    if (m_CueSheet) delete[] m_CueSheet;
}

// Helper: CloneCD standard is 75 frames/sec
int CCcdFileDevice::MsfToLba(int m, int s, int f) const {
    return ((m * 60) + s) * 75 + f;
}

bool CCcdFileDevice::Init() {
    if (!ParseCcd()) {
        LOGERR("Failed to parse CCD file");
        return false;
    }

    // Open files BEFORE generating CUE sheet so we can check file size
    FRESULT fr = f_open(m_ImgFile, m_ImgPath, FA_READ);
    if (fr != FR_OK) {
        LOGERR("Failed to open IMG file: %s (error %d)", m_ImgPath, fr);
        return false;
    }

    fr = f_open(m_SubFile, m_SubPath, FA_READ);
    if (fr != FR_OK) {
        LOGNOTE("SUB file not found or cannot open: %s (error %d). Subchannel data disabled.", m_SubPath, fr);
        delete m_SubFile;
        m_SubFile = nullptr;
    } else {
        LOGNOTE("SUB file opened: %s", m_SubPath);
    }

    // Now generate CUE (will calculate offsets based on file size)
    GenerateCueSheet();

    // ========================================================================
    // DIAGNOSTIC SECTION
    // ========================================================================
    LOGNOTE("CCD_DIAG", "========================================");
    LOGNOTE("CCD_DIAG", "CCD Initialization Diagnostic");
    LOGNOTE("CCD_DIAG", "========================================");
    
    // File sizes
    u64 imgSize = f_size(m_ImgFile);
    u64 subSize = m_SubFile ? f_size(m_SubFile) : 0;
    
    LOGNOTE("CCD_DIAG", "IMG file: %s", m_ImgPath);
    LOGNOTE("CCD_DIAG", "IMG size: %llu bytes (%u sectors)", 
            imgSize, (u32)(imgSize / 2352));
    
    if (m_SubFile) {
        LOGNOTE("CCD_DIAG", "SUB file: %s", m_SubPath);
        LOGNOTE("CCD_DIAG", "SUB size: %llu bytes (%u sectors)", 
                subSize, (u32)(subSize / 96));
        
        // Verify sizes match
        u32 imgSectors = (u32)(imgSize / 2352);
        u32 subSectors = (u32)(subSize / 96);
        if (imgSectors != subSectors) {
            LOGERR("CCD_DIAG", "WARNING: IMG and SUB sector counts don't match! IMG=%u, SUB=%u",
                   imgSectors, subSectors);
        } else {
            LOGNOTE("CCD_DIAG", "Sector counts match: %u sectors", imgSectors);
        }
    } else {
        LOGNOTE("CCD_DIAG", "SUB file: NOT AVAILABLE");
    }
    
    // Offsets
    LOGNOTE("CCD_DIAG", "Main data offset: %llu bytes (%u sectors)", 
            m_MainDataOffset, (u32)(m_MainDataOffset / 2352));
    LOGNOTE("CCD_DIAG", "Sub data offset: %llu bytes (%u sectors)", 
            m_SubDataOffset, (u32)(m_SubDataOffset / 96));
    
    // Track information
    LOGNOTE("CCD_DIAG", "Total tracks: %d", (int)m_Tracks.size());
    for (size_t i = 0; i < m_Tracks.size() && i < 5; ++i) {
        const TrackInfo& t = m_Tracks[i];
        LOGNOTE("CCD_DIAG", "  Track %d: LBA=%d, Len=%d, Type=%s",
                t.number, t.start_lba, t.length,
                t.is_audio ? "AUDIO" : "DATA");
    }
    
    LOGNOTE("CCD_DIAG", "========================================");
    
    // Validate subchannel format
    ValidateSubchannelFormat();
    
    // Dump subchannel data for key sectors
    if (m_SubFile) {
        LOGNOTE("CCD_DIAG", "----------------------------------------");
        LOGNOTE("CCD_DIAG", "Subchannel Data Samples");
        LOGNOTE("CCD_DIAG", "----------------------------------------");
        DumpSubchannelHex(0);    // Track 1 start
        DumpSubchannelHex(16);   // Common SafeDisc location
        if (m_Tracks[0].length > 150) {
            DumpSubchannelHex(150);  // 2 seconds in
        }
    }
    
    LOGNOTE("CCD_DIAG", "========================================");
    LOGNOTE("CCD_DIAG", "Diagnostic Complete");
    LOGNOTE("CCD_DIAG", "========================================");
    // ========================================================================
    // END DIAGNOSTIC SECTION
    // ========================================================================

    return true;
}

void CCcdFileDevice::ValidateSubchannelFormat() {
    if (!m_SubFile) {
        LOGNOTE("CCD_VAL", "No subchannel file to validate");
        return;
    }
    
    LOGNOTE("CCD_VAL", "Validating subchannel format...");
    
    u8 sub[96];
    bool allGood = true;
    
    // Check first 10 sectors
    for (int lba = 0; lba < 10; ++lba) {
        int res = ReadSubchannel(lba, sub);
        if (res != 96) {
            LOGERR("CCD_VAL", "LBA %d: Read failed (%d bytes)", lba, res);
            allGood = false;
            continue;
        }
        
        // Check Q-channel format (bytes 12-23)
        u8 adr = (sub[12] >> 4) & 0x0F;
        u8 ctl = sub[12] & 0x0F;
        u8 track = sub[13];
        u8 index = sub[14];
        
        // Check if Q-channel looks reasonable
        bool allZero = true;
        for (int i = 12; i < 24; i++) {
            if (sub[i] != 0) {
                allZero = false;
                break;
            }
        }
        
        if (allZero) {
            LOGERR("CCD_VAL", "LBA %d: Q-channel is all zeros - might be wrong format!", lba);
            allGood = false;
        } else {
            // ADR should typically be 1 (current position)
            if (adr != 1) {
                LOGNOTE("CCD_VAL", "LBA %d: Unusual ADR=%d (expected 1)", lba, adr);
            }
            
            // Track number should be valid (1-99)
            if (track == 0 || track > 99) {
                LOGERR("CCD_VAL", "LBA %d: Invalid track number 0x%02x in Q-channel", lba, track);
                allGood = false;
            } else {
                LOGNOTE("CCD_VAL", "LBA %d: Q[ADR=%d,CTL=%d,TRK=%02x,IDX=%02x] OK", 
                       lba, adr, ctl, track, index);
            }
        }
    }
    
    if (allGood) {
        LOGNOTE("CCD_VAL", "✓ Subchannel validation PASSED");
    } else {
        LOGERR("CCD_VAL", "✗ Subchannel validation FAILED - format may be incorrect");
    }
}

void CCcdFileDevice::DumpSubchannelHex(u32 lba) {
    if (!m_SubFile) return;
    
    u8 sub[96];
    int res = ReadSubchannel(lba, sub);
    if (res != 96) {
        LOGERR("CCD_DUMP", "Read failed for LBA %u", lba);
        return;
    }
    
    LOGNOTE("CCD_DUMP", "Subchannel data for LBA %u:", lba);
    
    // Dump Q-channel specifically (most important)
    LOGNOTE("CCD_DUMP", "  Q-channel (bytes 12-23):");
    char hex[80];
    snprintf(hex, sizeof(hex), "    %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             sub[12], sub[13], sub[14], sub[15], sub[16], sub[17],
             sub[18], sub[19], sub[20], sub[21], sub[22], sub[23]);
    LOGNOTE("CCD_DUMP", "%s", hex);
    
    // Decode Q-channel
    u8 adr = (sub[12] >> 4) & 0x0F;
    u8 ctl = sub[12] & 0x0F;
    u8 track = sub[13];
    u8 index = sub[14];
    
    LOGNOTE("CCD_DUMP", "  Decoded: ADR=%d, Control=0x%x, Track=%02x, Index=%02x",
            adr, ctl, track, index);
    
    // Show absolute time
    LOGNOTE("CCD_DUMP", "  Absolute time: %02x:%02x:%02x (BCD)", 
            sub[19], sub[20], sub[21]);
}

bool CCcdFileDevice::ParseCcd() {
    if (!m_CcdContent) return false;

    char* content = new char[strlen(m_CcdContent) + 1];
    strcpy(content, m_CcdContent);

    // Safer tokenizer handling
    char* line = strtok(content, "\n");
    CcdEntry currentEntry = {0};
    bool inEntry = false;

    while (line) {
        while (isspace(*line)) line++;

        if (beginsWith(line, "[Entry")) {
            if (inEntry) {
                // Fixup: Calculate PLBA from MSF if PLBA is missing/zero
                if (currentEntry.PLBA == 0 && (currentEntry.PMin != 0 || currentEntry.PSec != 0 || currentEntry.PFrame != 0)) {
                     currentEntry.PLBA = MsfToLba(currentEntry.PMin, currentEntry.PSec, currentEntry.PFrame);
                }
                m_Entries.push_back(currentEntry);
            }
            memset(&currentEntry, 0, sizeof(CcdEntry));
            inEntry = true;
        } else if (inEntry) {
            if (strstr(line, "Session")) currentEntry.Session = parseInt(line, "Session");
            else if (strstr(line, "Point")) currentEntry.Point = parseInt(line, "Point");
            else if (strstr(line, "ADR")) currentEntry.ADR = parseInt(line, "ADR");
            else if (strstr(line, "Control")) currentEntry.Control = parseInt(line, "Control");
            else if (strstr(line, "TrackNo")) currentEntry.TrackNo = parseInt(line, "TrackNo");
            else if (strstr(line, "PLBA")) currentEntry.PLBA = parseInt(line, "PLBA");
            else if (strstr(line, "PMin")) currentEntry.PMin = parseInt(line, "PMin");
            else if (strstr(line, "PSec")) currentEntry.PSec = parseInt(line, "PSec");
            else if (strstr(line, "PFrame")) currentEntry.PFrame = parseInt(line, "PFrame");
        }

        line = strtok(nullptr, "\n");
    }

    if (inEntry) {
        if (currentEntry.PLBA == 0 && (currentEntry.PMin != 0 || currentEntry.PSec != 0 || currentEntry.PFrame != 0)) {
             currentEntry.PLBA = MsfToLba(currentEntry.PMin, currentEntry.PSec, currentEntry.PFrame);
        }
        m_Entries.push_back(currentEntry);
    }

    delete[] content;

    std::sort(m_Entries.begin(), m_Entries.end(), [](const CcdEntry& a, const CcdEntry& b) {
        if (a.Session != b.Session) return a.Session < b.Session;
        return a.Point < b.Point;
    });

    return true;
}

void CCcdFileDevice::GenerateCueSheet() {
    std::vector<CcdEntry> trackEntries;
    for (const auto& entry : m_Entries) {
        if (entry.Session == 1 && entry.Point >= 1 && entry.Point <= 99) {
            trackEntries.push_back(entry);
        }
    }

    int leadOutLba = 0;
    for (const auto& entry : m_Entries) {
        if (entry.Session == 1 && entry.Point == 0xA2) {
            leadOutLba = entry.PLBA;
            break;
        }
    }

    u64 fileSize = f_size(m_ImgFile);
    u32 fileSectors = (u32)(fileSize / 2352);
    
    if (fileSectors <= (leadOutLba - 150 + 2)) {
        m_MainDataOffset = 0;
        m_SubDataOffset = 0;
        LOGNOTE("CCD: Detected RAW image WITHOUT pregap (File sectors: %u, Offset: 0)", fileSectors);
    } else {
        m_MainDataOffset = 150 * 2352;
        m_SubDataOffset = 150 * 96;
        LOGNOTE("CCD: Detected RAW image WITH pregap (File sectors: %u, Offset: 150)", fileSectors);
    }

    char* buffer = new char[16384];
    char* p = buffer;

    const char* imgName = strrchr(m_ImgPath, '/');
    if (imgName) imgName++; else imgName = m_ImgPath;

    p += sprintf(p, "FILE \"%s\" BINARY\n", imgName);

    // DON'T NORMALIZE LBAS - KEEP ABSOLUTE POSITIONS
    for (size_t i = 0; i < trackEntries.size(); ++i) {
        const auto& entry = trackEntries[i];
        
        // Use absolute LBA from CCD, don't subtract 150
        int absoluteLba = entry.PLBA;

        bool isData = (entry.Control & 0x04);
        const char* mode = isData ? "MODE1/2352" : "AUDIO";
        
        p += sprintf(p, "  TRACK %02d %s\n", entry.Point, mode);
        p += sprintf(p, "    INDEX 01 %02d:%02d:%02d\n",
                     absoluteLba / (75 * 60),
                     (absoluteLba / 75) % 60,
                     absoluteLba % 75);

        TrackInfo info;
        info.number = entry.Point;
        info.start_lba = absoluteLba;  // Store absolute LBA
        info.is_audio = !isData;
        info.pregap = (entry.Point == 1) ? 150 : 0;

        int nextPlba = (i < trackEntries.size() - 1) ? trackEntries[i+1].PLBA : leadOutLba;
        info.length = nextPlba - entry.PLBA;

        m_Tracks.push_back(info);
        
        LOGNOTE("Track %d: LBA %d (Abs), Len %d, Type %s", 
            info.number, info.start_lba, info.length, isData ? "DATA" : "AUDIO");
    }

    m_CueSheet = buffer;
}

int CCcdFileDevice::Read(void* pBuffer, size_t nCount) {
    if (!m_ImgFile) return 0;
    UINT bytesRead;
    FRESULT fr = f_read(m_ImgFile, pBuffer, nCount, &bytesRead);
    if (fr != FR_OK) return 0;
    m_CurrentOffset += bytesRead;
    return bytesRead;
}

int CCcdFileDevice::Write(const void* pBuffer, size_t nCount) {
    return 0; 
}

u64 CCcdFileDevice::Seek(u64 ullOffset) {
    if (!m_ImgFile) return 0;
    // ullOffset is in bytes, already calculated from absolute LBA
    FRESULT fr = f_lseek(m_ImgFile, ullOffset);
    if (fr == FR_OK) {
        m_CurrentOffset = ullOffset;
    }
    return m_CurrentOffset;
}

u64 CCcdFileDevice::GetSize(void) const {
    if (!m_ImgFile) return 0;
    
    // Use leadout from CCD to calculate size, not file size
    int leadOutLba = 0;
    for (const auto& entry : m_Entries) {
        if (entry.Session == 1 && entry.Point == 0xA2) {
            leadOutLba = entry.PLBA;
            break;
        }
    }
    
    if (leadOutLba == 0) {
        // Fallback
        u64 size = f_size(m_ImgFile);
        return (size > m_MainDataOffset) ? size - m_MainDataOffset : 0;
    }
    
    // Return logical size: (absolute_leadout - 150) * 2352
    int logicalSectors = leadOutLba - 150;
    return (u64)logicalSectors * 2352;
}

u64 CCcdFileDevice::Tell() const {
    return m_CurrentOffset;
}

int CCcdFileDevice::GetNumTracks() const {
    return m_Tracks.size();
}

u32 CCcdFileDevice::GetTrackStart(int track) const {
    if (track < 1 || track > (int)m_Tracks.size()) return 0;
    return m_Tracks[track - 1].start_lba;
}

u32 CCcdFileDevice::GetTrackLength(int track) const {
    if (track < 1 || track > (int)m_Tracks.size()) return 0;
    return m_Tracks[track - 1].length;
}

bool CCcdFileDevice::IsAudioTrack(int track) const {
    if (track < 1 || track > (int)m_Tracks.size()) return false;
    return m_Tracks[track - 1].is_audio;
}

int CCcdFileDevice::ReadSubchannel(u32 lba, u8* subchannel) {
    if (!m_SubFile) return -1;
    
    // LBA passed here is Logical.
    // Map to physical file offset by adding m_SubDataOffset (150 blocks).
    u64 offset = (u64)lba * 96 + m_SubDataOffset;

    // DEBUG: Log first 20 reads
    static int readCount = 0;
    if (readCount < 20) {
        LOGNOTE("CCD_SUB", "Read #%d: LBA=%u, offset=%llu (pregap_offset=%llu)",
                readCount, lba, offset, m_SubDataOffset);
    }

    FRESULT fr = f_lseek(m_SubFile, offset);
    if (fr != FR_OK) {
        LOGERR("CCD_SUB", "Seek failed: LBA=%u, offset=%llu, err=%d", lba, offset, fr);
        return -1;
    }

    UINT bytesRead;
    fr = f_read(m_SubFile, subchannel, 96, &bytesRead);
    if (fr != FR_OK || bytesRead != 96) {
        LOGERR("CCD_SUB", "Read failed: LBA=%u, bytes=%u, err=%d", lba, bytesRead, fr);
        return -1;
    }
    
    // DEBUG: Log Q-channel data for first reads
    if (readCount < 20) {
        LOGNOTE("CCD_SUB", "  Q: ADR=%d, CTL=%d, TRK=%02x, IDX=%02x, AbsTime=%02x:%02x:%02x",
                (subchannel[12] >> 4) & 0xF, subchannel[12] & 0xF,
                subchannel[13], subchannel[14],
                subchannel[19], subchannel[20], subchannel[21]);
        readCount++;
    }

    return 96;
}