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

    GenerateCueSheet();

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

    return true;
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

    // CloneCD images include the 150 frame pregap (2 seconds).
    // We set an internal offset to skip it for seek operations,
    // effectively normalizing the file to start at Logical LBA 0.
    m_MainDataOffset = 150 * 2352;
    m_SubDataOffset = 150 * 96;

    char* buffer = new char[16384];
    char* p = buffer;

    const char* imgName = strrchr(m_ImgPath, '/');
    if (imgName) imgName++; else imgName = m_ImgPath;

    p += sprintf(p, "FILE \"%s\" BINARY\n", imgName);

    for (size_t i = 0; i < trackEntries.size(); ++i) {
        const auto& entry = trackEntries[i];
        
        // Normalize LBA to be 0-based for the Gadget.
        // CloneCD uses Absolute LBA (150-based).
        // By subtracting 150, Track 1 will start at 0.
        int logicalLba = entry.PLBA - 150;
        if (logicalLba < 0) logicalLba = 0; 

        bool isData = (entry.Control & 0x04);
        const char* mode = isData ? "MODE1/2352" : "AUDIO";
        
        p += sprintf(p, "  TRACK %02d %s\n", entry.Point, mode);
        
        // FIX: Use logicalLba here. 
        // Previously used entry.PLBA (150), which caused CUE to say "00:02:00".
        // The Gadget would then read that as LBA 150, and add another 150 for TOC,
        // resulting in 00:04:00 (LBA 300).
        // By using logicalLba (0), CUE says "00:00:00". Gadget sees LBA 0.
        // Gadget adds 150 for TOC -> 00:02:00. Correct.
        p += sprintf(p, "    INDEX 01 %02d:%02d:%02d\n",
                     logicalLba / (75 * 60),
                     (logicalLba / 75) % 60,
                     logicalLba % 75);

        TrackInfo info;
        info.number = entry.Point;
        info.start_lba = logicalLba; 
        info.is_audio = !isData;
        info.pregap = 0;

        int nextPlba = (i < trackEntries.size() - 1) ? trackEntries[i+1].PLBA : leadOutLba;
        info.length = nextPlba - entry.PLBA;

        m_Tracks.push_back(info);
        
        LOGNOTE("Track %d: LBA %d (Abs %d), Len %d, Type %s", 
            info.number, info.start_lba, entry.PLBA, info.length, isData ? "DATA" : "AUDIO");
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
    // Offset passed here is Logical (relative to Track 1 start).
    // We add m_MainDataOffset (150 blocks) to skip the physical pregap in the file.
    FRESULT fr = f_lseek(m_ImgFile, ullOffset + m_MainDataOffset);
    if (fr == FR_OK) {
        m_CurrentOffset = ullOffset;
    }
    return m_CurrentOffset;
}

u64 CCcdFileDevice::GetSize(void) const {
    if (!m_ImgFile) return 0;
    u64 size = f_size(m_ImgFile);
    return (size > m_MainDataOffset) ? size - m_MainDataOffset : 0;
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

    FRESULT fr = f_lseek(m_SubFile, offset);
    if (fr != FR_OK) return -1;

    UINT bytesRead;
    fr = f_read(m_SubFile, subchannel, 96, &bytesRead);
    if (fr != FR_OK || bytesRead != 96) return -1;

    return 96;
}