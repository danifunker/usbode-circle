/*
 * Simple CUE sheet parser suitable for embedded systems.
 *
 * Copyright (c) 2025 Ian Cass - bundled for USBODE
 * Copyright (c) 2023 Rabbit Hole Computing
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

// Refer to e.g. https://www.gnu.org/software/ccd2cue/manual/html_node/CUE-sheet-format.html#CUE-sheet-format
//
// Example of a CUE file:
// FILE "foo bar.bin" BINARY
//   TRACK 01 MODE1/2048
//     INDEX 01 00:00:00
//   TRACK 02 AUDIO
//     PREGAP 00:02:00
//     INDEX 01 02:47:20
//   TRACK 03 AUDIO
//     INDEX 00 07:55:58
//     INDEX 01 07:55:65

#include "cueparser.h"
// #include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <circle/logger.h>

LOGMODULE("cueparser");

CUEParser::CUEParser() : CUEParser("") {
}

CUEParser::CUEParser(const char *cue_sheet) : m_cue_sheet(cue_sheet) {
    restart();
}

void CUEParser::restart() {
    m_parse_pos = m_cue_sheet;
    memset(&m_track_info, 0, sizeof(m_track_info));
}

const CUETrackInfo *CUEParser::next_track() {
    return next_track(0);
}

const CUETrackInfo *CUEParser::next_track(uint64_t prev_file_size) {
    // Previous track info is needed to track file offset
    uint32_t prev_track_start = m_track_info.track_start;
    m_track_info.cumulative_offset += m_track_info.unstored_pregap_length;
    uint32_t prev_sector_length = get_sector_length(m_track_info.file_mode, m_track_info.track_mode);  // Defaults to 2352 before first track

    bool got_file = false;
    bool got_track = false;
    bool got_data = false;
    bool got_pause = false;  // true if a period of silence (INDEX 00) was encountered for a track
    while (!(got_track && got_data) && start_line()) {
        if (strncasecmp(m_parse_pos, "FILE ", 5) == 0) {
            if (m_track_info.file_index > 0) {
                // Take into account the length of last track in previous file.
                uint32_t last_track_blocks = (prev_file_size - m_track_info.file_offset) / m_track_info.sector_length;
                m_track_info.file_start = m_track_info.data_start + last_track_blocks;
            }

            const char *p = read_quoted(m_parse_pos + 5, m_track_info.filename, sizeof(m_track_info.filename));
            remove_dot_slash(m_track_info.filename, sizeof(m_track_info.filename));
            m_track_info.file_mode = parse_file_mode(skip_space(p));
            m_track_info.file_offset = 0;
            m_track_info.file_index++;
            m_track_info.track_mode = CUETrack_AUDIO;
            prev_track_start = 0;
            prev_sector_length = get_sector_length(m_track_info.file_mode, m_track_info.track_mode);
            got_file = true;
        } else if (strncasecmp(m_parse_pos, "TRACK ", 6) == 0) {
            const char *track_num = skip_space(m_parse_pos + 6);
            char *endptr;
            m_track_info.track_number = strtoul(track_num, &endptr, 10);
            LOGNOTE("Parsed track number: %d", m_track_info.track_number);
            m_track_info.track_mode = parse_track_mode(skip_space(endptr));
            m_track_info.sector_length = get_sector_length(m_track_info.file_mode, m_track_info.track_mode);
            m_track_info.unstored_pregap_length = 0;
            m_track_info.data_start = 0;
            m_track_info.track_start = 0;
            got_track = true;
            got_data = false;
            got_pause = false;
        } else if (strncasecmp(m_parse_pos, "PREGAP ", 7) == 0) {
            // Unstored pregap, which offsets the data start on CD but does not
            // affect the offset in data file.
            const char *time_str = skip_space(m_parse_pos + 7);
            m_track_info.unstored_pregap_length = parse_time(time_str);
        } else if (strncasecmp(m_parse_pos, "INDEX ", 6) == 0) {
            const char *index_str = skip_space(m_parse_pos + 6);
            char *endptr;
            int index = strtoul(index_str, &endptr, 10);

            const char *time_str = skip_space(endptr);
            uint32_t time = parse_time(time_str);

            if (index == 0) {
                // Stored pregap that is present both on CD and in data file
                m_track_info.track_start = m_track_info.file_start + time + m_track_info.cumulative_offset;
                got_pause = true;
            } else if (index == 1) {
                // Data content of the track
                m_track_info.data_start = m_track_info.file_start + time + m_track_info.cumulative_offset;
                got_data = true;
            }
        }

        next_line();
    }

    if (got_data && !got_pause) {
        m_track_info.track_start = m_track_info.data_start;
        m_track_info.data_start += m_track_info.unstored_pregap_length;
    }

    if (got_track && got_data) {
        if (!got_file) {
            // Advance file position by the length of previous track
            m_track_info.file_offset += (uint64_t)(m_track_info.track_start - (prev_track_start + m_track_info.cumulative_offset)) * prev_sector_length;
        }

        // Advance file position by any stored pregap
        uint32_t stored_pregap = m_track_info.data_start - (m_track_info.track_start + m_track_info.unstored_pregap_length);
        m_track_info.file_offset += (uint64_t)stored_pregap * m_track_info.sector_length;

        return &m_track_info;
    } else {
        return nullptr;
    }
}

bool isspace(char c) {
    // Check for space, tab, newline, vertical tab, form feed, carriage return
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

bool CUEParser::start_line() {
    // Skip initial whitespace
    while (isspace(*m_parse_pos)) {
        m_parse_pos++;
    }
    return *m_parse_pos != '\0';
}

void CUEParser::next_line() {
    // Find end of current line
    const char *p = m_parse_pos;
    while (*p != '\n' && *p != '\0') {
        p++;
    }

    // Skip any linefeeds
    while (*p == '\n' || *p == '\r') {
        p++;
    }

    m_parse_pos = p;
}

const char *CUEParser::skip_space(const char *p) const {
    while (isspace(*p)) p++;
    return p;
}

const char *CUEParser::read_quoted(const char *src, char *dest, int dest_size) {
    // Search for starting quote
    while (*src != '"') {
        if (*src == '\0' || *src == '\n') {
            // Unexpected end of line / file
            dest[0] = '\0';
            return src;
        }

        src++;
    }

    src++;

    // Copy text until ending quote
    int len = 0;
    while (*src != '"' && *src != '\0' && *src != '\n') {
        if (len < dest_size - 1) {
            dest[len++] = *src;
        }

        src++;
    }

    dest[len] = '\0';

    if (*src == '"') src++;
    return src;
}

uint32_t CUEParser::parse_time(const char *src) {
    char *endptr;
    uint32_t minutes = strtoul(src, &endptr, 10);
    if (*endptr == ':') endptr++;
    uint32_t seconds = strtoul(endptr, &endptr, 10);
    if (*endptr == ':') endptr++;
    uint32_t frames = strtoul(endptr, &endptr, 10);

    return frames + 75 * (seconds + 60 * minutes);
}

CUEFileMode CUEParser::parse_file_mode(const char *src) {
    if (strncasecmp(src, "BIN", 3) == 0)
        return CUEFile_BINARY;
    else if (strncasecmp(src, "MOTOROLA", 8) == 0)
        return CUEFile_MOTOROLA;
    else if (strncasecmp(src, "MP3", 3) == 0)
        return CUEFile_MP3;
    else if (strncasecmp(src, "WAV", 3) == 0)
        return CUEFile_WAVE;
    else if (strncasecmp(src, "AIFF", 4) == 0)
        return CUEFile_AIFF;
    else
        return CUEFile_BINARY;  // Default to binary mode
}

CUETrackMode CUEParser::parse_track_mode(const char *src) {
    if (strncasecmp(src, "AUDIO", 5) == 0)
        return CUETrack_AUDIO;
    else if (strncasecmp(src, "CDG", 3) == 0)
        return CUETrack_CDG;
    else if (strncasecmp(src, "MODE1/2048", 10) == 0)
        return CUETrack_MODE1_2048;
    else if (strncasecmp(src, "MODE1/2352", 10) == 0)
        return CUETrack_MODE1_2352;
    else if (strncasecmp(src, "MODE2/2048", 10) == 0)
        return CUETrack_MODE2_2048;
    else if (strncasecmp(src, "MODE2/2324", 10) == 0)
        return CUETrack_MODE2_2324;
    else if (strncasecmp(src, "MODE2/2336", 10) == 0)
        return CUETrack_MODE2_2336;
    else if (strncasecmp(src, "MODE2/2352", 10) == 0)
        return CUETrack_MODE2_2352;
    else if (strncasecmp(src, "CDI/2336", 8) == 0)
        return CUETrack_CDI_2336;
    else if (strncasecmp(src, "CDI/2352", 8) == 0)
        return CUETrack_CDI_2352;
    else
        return CUETrack_MODE1_2048;  // Default to data track
}

uint32_t CUEParser::get_sector_length(CUEFileMode filemode, CUETrackMode trackmode) {
    if (filemode == CUEFile_BINARY || filemode == CUEFile_MOTOROLA) {
        switch (trackmode) {
            case CUETrack_AUDIO:
                return 2352;
            case CUETrack_CDG:
                return 2448;
            case CUETrack_MODE1_2048:
                return 2048;
            case CUETrack_MODE1_2352:
                return 2352;
            case CUETrack_MODE2_2048:
                return 2048;
            case CUETrack_MODE2_2324:
                return 2324;
            case CUETrack_MODE2_2336:
                return 2336;
            case CUETrack_MODE2_2352:
                return 2352;
            case CUETrack_CDI_2336:
                return 2336;
            case CUETrack_CDI_2352:
                return 2352;
            default:
                return 2048;
        }
    } else {
        return 0;
    }
}

void CUEParser::remove_dot_slash(char *filename, size_t length) {
    if (strncasecmp(filename, "./", 2) == 0 || strncasecmp(filename, ".\\", 2) == 0) {
        memmove(filename, filename + 2, length - 2);
    }
}
