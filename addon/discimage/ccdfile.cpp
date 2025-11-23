#include "ccdfile.h"
#include <circle/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

LOGMODULE("CCcdFileDevice");

// Helper function to read a line from a string buffer
static char* read_line(char** context) {
    if (!*context || **context == '\0') {
        return NULL;
    }
    char* start = *context;
    char* end = strchr(start, '\n');
    if (end) {
        *end = '\0';
        *context = end + 1;
    } else {
        *context = start + strlen(start);
    }
    // Strip carriage return if present
    char* cr = strchr(start, '\r');
    if (cr) {
        *cr = '\0';
    }
    return start;
}

CCcdFileDevice::CCcdFileDevice(const char* ccd_filename) :
    m_imgFile(nullptr),
    m_subFile(nullptr),
    m_cueSheet(nullptr),
    m_hasSubchannels(false),
    m_tracks(nullptr),
    m_numTracks(0)
{
    snprintf(m_ccd_filename, sizeof(m_ccd_filename), "%s", ccd_filename);
}

CCcdFileDevice::~CCcdFileDevice(void) {
    if (m_imgFile) {
        f_close(m_imgFile);
        delete m_imgFile;
    }
    if (m_subFile) {
        f_close(m_subFile);
        delete m_subFile;
    }
    delete[] m_cueSheet;
    delete[] m_tracks;
}

bool CCcdFileDevice::Init() {
    char ccd_path[255];
    snprintf(ccd_path, sizeof(ccd_path), "1:/%s", m_ccd_filename);

    if (!ParseCcdFile(ccd_path)) {
        LOGERR("Failed to parse CCD file: %s", ccd_path);
        return false;
    }

    GenerateCueSheet();
    return true;
}

bool CCcdFileDevice::ParseCcdFile(const char* ccd_path) {
    // Read the entire CCD file into memory
    FIL file;
    FRESULT res = f_open(&file, ccd_path, FA_READ);
    if (res != FR_OK) {
        LOGERR("Cannot open CCD file: %s", ccd_path);
        return false;
    }

    DWORD size = f_size(&file);
    char* ccd_buffer = new char[size + 1];
    UINT bytes_read;
    f_read(&file, ccd_buffer, size, &bytes_read);
    f_close(&file);
    ccd_buffer[bytes_read] = '\0';

    char* context = ccd_buffer;
    char* line;
    int current_track = -1;
    int max_tracks = 0;

    // First pass: count tracks to allocate memory (non-destructive)
    char* temp_buffer = new char[size + 1];
    memcpy(temp_buffer, ccd_buffer, size + 1);
    char* temp_context = temp_buffer;
    while ((line = read_line(&temp_context)) != NULL) {
        if (strncmp(line, "[TRACK ", 7) == 0) {
            max_tracks++;
        }
    }
    delete[] temp_buffer;

    if (max_tracks == 0) {
        LOGERR("No tracks found in CCD file");
        delete[] ccd_buffer;
        return false;
    }

    m_tracks = new TrackInfo[max_tracks];
    memset(m_tracks, 0, sizeof(TrackInfo) * max_tracks);
    m_numTracks = max_tracks;

    // Second pass: parse track info
    context = ccd_buffer; // Reset context
    while ((line = read_line(&context)) != NULL) {
        if (strncmp(line, "[TRACK ", 7) == 0) {
            char* endptr;
            long val = strtol(line + 7, &endptr, 10);
            if (endptr == line + 7 || (*endptr != ']' && *endptr != '\0')) {
                LOGERR("Malformed track number: %s", line);
                continue;
            }
            current_track = val - 1;
            if (current_track >= max_tracks || current_track < 0) {
                LOGERR("Track number out of bounds: %ld", val);
                current_track = -1;
                continue;
            }
        } else if (current_track != -1) {
            if (strncmp(line, "MODE=", 5) == 0) {
                 m_tracks[current_track].is_audio = (atoi(line + 5) == 0);
            } else if (strncmp(line, "INDEX 01=", 9) == 0) {
                char* endptr;
                long long val = strtoll(line + 9, &endptr, 10);
                if (endptr == line + 9 || *endptr != '\0') {
                    LOGERR("Malformed LBA: %s", line);
                    continue;
                }
                m_tracks[current_track].start_lba = val;
            }
        }
    }

    // Calculate track lengths
    for (int i = 0; i < m_numTracks - 1; i++) {
        m_tracks[i].length = m_tracks[i+1].start_lba - m_tracks[i].start_lba;
    }

    // Open the IMG file
    char img_path[255];
    const char* ext = strrchr(ccd_path, '.');
    int base_len = ext ? (ext - ccd_path) : strlen(ccd_path);
    snprintf(img_path, sizeof(img_path), "%.*s.img", base_len, ccd_path);

    m_imgFile = new FIL();
    res = f_open(m_imgFile, img_path, FA_READ);
    if (res != FR_OK) {
        LOGERR("Cannot open IMG file: %s", img_path);
        delete m_imgFile;
        m_imgFile = nullptr;
        delete[] ccd_buffer;
        return false;
    }

    // Set length of last track
    if (m_numTracks > 0) {
        m_tracks[m_numTracks-1].length = (f_size(m_imgFile) / 2352) - m_tracks[m_numTracks-1].start_lba;
    }

    // Open the SUB file
    char sub_path[255];
    snprintf(sub_path, sizeof(sub_path), "%.*s.sub", base_len, ccd_path);

    m_subFile = new FIL();
    res = f_open(m_subFile, sub_path, FA_READ);
    if (res == FR_OK) {
        m_hasSubchannels = true;
    } else {
        delete m_subFile;
        m_subFile = nullptr;
    }

    delete[] ccd_buffer;
    return true;
}

void CCcdFileDevice::GenerateCueSheet() {
    size_t cue_size = 1024 + (m_numTracks * 128); // Estimate size
    char* cue_buffer = new char[cue_size];
    char* cue_ptr = cue_buffer;
    int remaining = cue_size;

    char img_filename[255];
    const char* ext = strrchr(m_ccd_filename, '.');
    int base_len = ext ? (ext - m_ccd_filename) : strlen(m_ccd_filename);
    snprintf(img_filename, sizeof(img_filename), "%.*s.img", base_len, m_ccd_filename);

    int len = snprintf(cue_ptr, remaining, "FILE \"%s\" BINARY\n", img_filename);
    cue_ptr += len;
    remaining -= len;

    for (int i = 0; i < m_numTracks; i++) {
        const char* mode_str = m_tracks[i].is_audio ? "AUDIO" : "MODE1/2352";
        len = snprintf(cue_ptr, remaining, "  TRACK %02d %s\n", i + 1, mode_str);
        cue_ptr += len;
        remaining -= len;

        u32 lba = m_tracks[i].start_lba;
        int minutes = lba / (75 * 60);
        int seconds = (lba / 75) % 60;
        int frames = lba % 75;
        len = snprintf(cue_ptr, remaining, "    INDEX 01 %02d:%02d:%02d\n", minutes, seconds, frames);
        cue_ptr += len;
        remaining -= len;
    }

    m_cueSheet = new char[strlen(cue_buffer) + 1];
    snprintf(m_cueSheet, strlen(cue_buffer) + 1, "%s", cue_buffer);
    delete[] cue_buffer;
}

int CCcdFileDevice::Read(void* pBuffer, size_t nSize) {
    if (!m_imgFile) return -1;
    UINT bytes_read;
    f_read(m_imgFile, pBuffer, nSize, &bytes_read);
    return bytes_read;
}

int CCcdFileDevice::Write(const void* pBuffer, size_t nSize) {
    return -1; // Read-only
}

u64 CCcdFileDevice::Seek(u64 ullOffset) {
    if (!m_imgFile) return -1;
    f_lseek(m_imgFile, ullOffset);
    return f_tell(m_imgFile);
}

u64 CCcdFileDevice::GetSize(void) const {
    if (!m_imgFile) return 0;
    return f_size(m_imgFile);
}

u64 CCcdFileDevice::Tell() const {
    if (!m_imgFile) return -1;
    return f_tell(m_imgFile);
}

int CCcdFileDevice::GetNumTracks() const {
    return m_numTracks;
}

u32 CCcdFileDevice::GetTrackStart(int track) const {
    if (track < 0 || track >= m_numTracks) return 0;
    return m_tracks[track].start_lba;
}

u32 CCcdFileDevice::GetTrackLength(int track) const {
    if (track < 0 || track >= m_numTracks) return 0;
    return m_tracks[track].length;
}

bool CCcdFileDevice::IsAudioTrack(int track) const {
    if (track < 0 || track >= m_numTracks) return false;
    return m_tracks[track].is_audio;
}

const char* CCcdFileDevice::GetCueSheet() const {
    return m_cueSheet;
}

bool CCcdFileDevice::HasSubchannelData() const {
    return m_hasSubchannels;
}

int CCcdFileDevice::ReadSubchannel(u32 lba, u8* subchannel) {
    if (!m_hasSubchannels || !m_subFile) {
        return -1;
    }

    u64 offset = (u64)lba * 96;
    if (f_lseek(m_subFile, offset) != FR_OK) {
        return -1;
    }

    UINT bytes_read;
    if (f_read(m_subFile, subchannel, 96, &bytes_read) != FR_OK || bytes_read != 96) {
        return -1;
    }

    return bytes_read;
}
