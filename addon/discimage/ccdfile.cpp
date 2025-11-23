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
    while (*p && (*p == ' ' || *p == '=')) p++;

    char* value = new char[strlen(p) + 1];
    strcpy(value, p);

    // Trim trailing whitespace/newline
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
    int res = strtol(val, nullptr, 0); // handles hex (0x) and decimal
    delete[] val;
    return res;
}

CCcdFileDevice::CCcdFileDevice(const char* imgPath, const char* subPath, const char* ccdContent, MEDIA_TYPE mediaType)
    : m_ImgPath(nullptr), m_SubPath(nullptr), m_CcdContent(ccdContent), m_CueSheet(nullptr), m_CurrentOffset(0), m_MediaType(mediaType) {

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
    if (m_CcdContent) delete[] m_CcdContent;
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

    // Make a copy to tokenize
    char* content = new char[strlen(m_CcdContent) + 1];
    strcpy(content, m_CcdContent);

    char* line = strtok(content, "\n");
    CcdEntry currentEntry = {0};
    bool inEntry = false;

    while (line) {
        // Trim leading whitespace
        while (isspace(*line)) line++;

        if (beginsWith(line, "[Entry")) {
            if (inEntry) {
                m_Entries.push_back(currentEntry);
            }
            memset(&currentEntry, 0, sizeof(CcdEntry));
            inEntry = true;
        } else if (inEntry) {
            if (beginsWith(line, "Session=")) currentEntry.Session = parseInt(line, "Session=");
            else if (beginsWith(line, "Point=")) currentEntry.Point = parseInt(line, "Point=");
            else if (beginsWith(line, "ADR=")) currentEntry.ADR = parseInt(line, "ADR=");
            else if (beginsWith(line, "Control=")) currentEntry.Control = parseInt(line, "Control=");
            else if (beginsWith(line, "TrackNo=")) currentEntry.TrackNo = parseInt(line, "TrackNo=");
            else if (beginsWith(line, "PLBA=")) currentEntry.PLBA = parseInt(line, "PLBA=");
            // Add other fields if needed
        }

        line = strtok(nullptr, "\n");
    }

    if (inEntry) {
        m_Entries.push_back(currentEntry);
    }

    delete[] content;

    // Sort entries
    std::sort(m_Entries.begin(), m_Entries.end(), [](const CcdEntry& a, const CcdEntry& b) {
        if (a.Session != b.Session) return a.Session < b.Session;

        // Special sorting order: 0xA0, 0xA1, 0xA2 should be handled carefully
        // But for standard track listing, Point 1..99 are tracks.
        // 0xA0 (first track), 0xA1 (last track), 0xA2 (leadout)

        // Simple sort by Point is usually enough if we process them correctly later.
        // Note: 0xA0=160, 0xA1=161, 0xA2=162.
        return a.Point < b.Point;
    });

    return true;
}

void CCcdFileDevice::GenerateCueSheet() {
    // Calculate tracks from Entries
    // We need entries with Session 1 (assume single session for now)
    // Tracks have Point 1..99

    std::vector<CcdEntry> trackEntries;
    for (const auto& entry : m_Entries) {
        if (entry.Session == 1 && entry.Point >= 1 && entry.Point <= 99) {
            trackEntries.push_back(entry);
        }
    }

    // Find Lead-out (0xA2) to calculate last track length
    int leadOutLba = 0;
    for (const auto& entry : m_Entries) {
        if (entry.Session == 1 && entry.Point == 0xA2) {
            leadOutLba = entry.PLBA;
            break;
        }
    }

    // Construct Cue Sheet buffer
    // Estimate size: 1024 header + 100 tracks * 100 bytes
    char* buffer = new char[16384];
    char* p = buffer;

    // Assuming IMG is always available
    // Strip path from img filename for CUE FILE entry
    const char* imgName = strrchr(m_ImgPath, '/');
    if (imgName) imgName++; else imgName = m_ImgPath;

    p += sprintf(p, "FILE \"%s\" BINARY\n", imgName);

    int numTracks = trackEntries.size();

    // If we didn't find leadOutLba, try to use file size
    if (leadOutLba == 0 && m_ImgFile) {
        // This is called before file open in Init(), but we can check size if file is open?
        // No, file is not open yet.
        // We rely on CCD entries. If 0xA2 is missing, we might have issues with last track length.
    }

    for (size_t i = 0; i < trackEntries.size(); ++i) {
        const auto& entry = trackEntries[i];
        int lba = entry.PLBA;

        // Determine mode
        bool isData = (entry.Control & 0x04);
        const char* mode = isData ? "MODE1/2352" : "AUDIO";

        p += sprintf(p, "  TRACK %02d %s\n", entry.Point, mode);
        p += sprintf(p, "    INDEX 01 %02d:%02d:%02d\n",
                     lba / (75 * 60),
                     (lba / 75) % 60,
                     lba % 75);

        TrackInfo info;
        info.number = entry.Point;
        info.start_lba = lba;
        info.is_audio = !isData;
        info.pregap = 0;

        if (i < trackEntries.size() - 1) {
            info.length = trackEntries[i+1].PLBA - lba;
        } else {
            info.length = leadOutLba - lba;
        }

        m_Tracks.push_back(info);
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
    return 0; // Read-only
}

u64 CCcdFileDevice::Seek(u64 ullOffset) {
    if (!m_ImgFile) return 0;
    FRESULT fr = f_lseek(m_ImgFile, ullOffset);
    if (fr == FR_OK) {
        m_CurrentOffset = ullOffset;
    }
    return m_CurrentOffset;
}

u64 CCcdFileDevice::GetSize(void) const {
    if (!m_ImgFile) return 0;
    return f_size(m_ImgFile);
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

    // Subchannel data is 96 bytes per sector
    u64 offset = (u64)lba * 96;

    // Save current position?
    // Since we use a separate file handle, we should be fine, but we need to seek.
    // Note: Circle's f_lseek modifies the file object state.
    // We should check if ReadSubchannel is called from a different thread or context than Read.
    // Assuming single-threaded access for now or guarded by caller.
    // BUT, m_SubFile is separate from m_ImgFile, so it's safe w.r.t data reads.

    FRESULT fr = f_lseek(m_SubFile, offset);
    if (fr != FR_OK) return -1;

    UINT bytesRead;
    fr = f_read(m_SubFile, subchannel, 96, &bytesRead);
    if (fr != FR_OK || bytesRead != 96) return -1;

    return 96;
}
