/*
 * CUE Sheet Parser for Embedded Systems
 *
 * A complete rewrite inspired by 86Box's CD-ROM image handling.
 * Designed for bare-metal embedded systems with minimal dependencies.
 *
 * Copyright (c) 2025 USBODE Project Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * CUE Sheet Format Reference:
 * https://www.gnu.org/software/ccd2cue/manual/html_node/CUE-sheet-format.html
 *
 * Example CUE file:
 *   FILE "disc1.bin" BINARY
 *     TRACK 01 MODE1/2352
 *       INDEX 01 00:00:00
 *     TRACK 02 AUDIO
 *       FLAGS DCP
 *       PREGAP 00:02:00
 *       INDEX 01 04:32:10
 *   FILE "disc2.bin" BINARY
 *     TRACK 03 AUDIO
 *       INDEX 00 00:00:00
 *       INDEX 01 00:02:00
 */

#include "cueparser.h"
#include <string.h>

// Frames per second on CD (75 frames = 1 second)
#define CD_FRAMES_PER_SECOND 75
#define CD_SECONDS_PER_MINUTE 60

CUEParser::CUEParser() : m_cue_sheet(nullptr), m_track_count(0), m_file_count(0),
                         m_current_track(0), m_parsed(false) {
    memset(&m_disc_info, 0, sizeof(m_disc_info));
    memset(m_tracks, 0, sizeof(m_tracks));
    memset(m_files, 0, sizeof(m_files));
    memset(&m_iter_info, 0, sizeof(m_iter_info));
}

int CUEParser::get_track_count() const {
    return m_track_count;
}

const CUEDiscInfo *CUEParser::get_disc_info() const {
    return &m_disc_info;
}

const char *CUEParser::get_catalog() const {
    return m_disc_info.catalog;
}

const char *CUEParser::get_cdtextfile() const {
    return m_disc_info.cdtextfile;
}

const CUECDText *CUEParser::get_disc_cdtext() const {
    return &m_disc_info.cdtext;
}

const CUERem *CUEParser::get_disc_rem() const {
    return &m_disc_info.rem;
}

CUEParser::CUEParser(const char *cue_sheet) : CUEParser() {
    m_cue_sheet = cue_sheet;
    if (cue_sheet && *cue_sheet) {
        parse_cue_sheet();
    }
}

void CUEParser::restart() {
    m_current_track = 0;
}

const CUETrackInfo *CUEParser::next_track() {
    return next_track(0);
}

const CUETrackInfo *CUEParser::next_track(uint64_t prev_file_size) {
    if (m_current_track >= m_track_count) {
        return nullptr;
    }

    // Update file size if provided (for multi-bin LBA calculation)
    if (prev_file_size > 0 && m_current_track > 0) {
        int prev_file_idx = m_tracks[m_current_track - 1].info.file_index;
        if (prev_file_idx >= 0 && prev_file_idx < m_file_count) {
            if (m_files[prev_file_idx].size == 0) {
                m_files[prev_file_idx].size = prev_file_size;
                recalculate_lba_positions();
            }
        }
    }

    // Copy track info to iterator buffer
    memcpy(&m_iter_info, &m_tracks[m_current_track].info, sizeof(CUETrackInfo));
    m_current_track++;

    return &m_iter_info;
}

const CUETrackInfo *CUEParser::get_track(int track_number) {
    for (int i = 0; i < m_track_count; i++) {
        if (m_tracks[i].info.track_number == track_number) {
            return &m_tracks[i].info;
        }
    }
    return nullptr;
}

void CUEParser::set_file_size(int file_index, uint64_t size) {
    if (file_index >= 0 && file_index < m_file_count) {
        m_files[file_index].size = size;
        recalculate_lba_positions();
    }
}

void CUEParser::parse_cue_sheet() {
    if (!m_cue_sheet || m_parsed) {
        return;
    }

    const char *pos = m_cue_sheet;
    char token[CUE_MAX_FILENAME + 1];
    char filename[CUE_MAX_FILENAME + 1];

    int current_file_index = -1;
    CUEFileMode current_file_mode = CUEFile_BINARY;
    CUEParsedTrack *current_track = nullptr;

    uint32_t cumulative_pregap = 0;

    // Process line by line
    while (*pos) {
        pos = skip_whitespace(pos);
        if (*pos == '\0') break;

        // Extract the command keyword
        pos = extract_token(pos, token, sizeof(token), true);

        if (str_equal_nocase(token, "FILE", 4)) {
            // FILE "filename" BINARY|MOTOROLA|AIFF|WAVE|MP3
            pos = extract_token(pos, filename, sizeof(filename), false);

            // Remove ./ or .\ prefix if present
            char *fn = filename;
            if ((fn[0] == '.' && fn[1] == '/') || (fn[0] == '.' && fn[1] == '\\')) {
                fn += 2;
            }

            pos = extract_token(pos, token, sizeof(token), true);
            current_file_mode = parse_file_mode(token);

            // Add new file entry
            if (m_file_count < CUE_MAX_TRACKS) {
                CUEFileEntry *fe = &m_files[m_file_count];
                strncpy(fe->filename, fn, CUE_MAX_FILENAME);
                fe->filename[CUE_MAX_FILENAME] = '\0';
                fe->mode = current_file_mode;
                fe->size = 0;
                current_file_index = m_file_count;
                m_file_count++;
            }

        } else if (str_equal_nocase(token, "TRACK", 5)) {
            // TRACK nn MODE1/2352|AUDIO|etc
            if (m_track_count >= CUE_MAX_TRACKS) {
                pos = next_line(pos);
                continue;
            }

            pos = extract_token(pos, token, sizeof(token), false);
            int track_num = 0;
            for (const char *p = token; *p >= '0' && *p <= '9'; p++) {
                track_num = track_num * 10 + (*p - '0');
            }

            pos = extract_token(pos, token, sizeof(token), true);
            uint32_t sector_size = 0;
            CUETrackMode track_mode = parse_track_mode(token, &sector_size);

            // Initialize new track
            current_track = &m_tracks[m_track_count];
            memset(current_track, 0, sizeof(CUEParsedTrack));

            current_track->info.track_number = track_num;
            current_track->info.track_mode = track_mode;
            current_track->info.sector_length = sector_size;
            current_track->info.file_index = current_file_index;
            current_track->info.file_mode = current_file_mode;
            current_track->info.cumulative_offset = cumulative_pregap;

            // Copy filename from current file
            if (current_file_index >= 0 && current_file_index < m_file_count) {
                strncpy(current_track->info.filename, m_files[current_file_index].filename, CUE_MAX_FILENAME);
                current_track->info.filename[CUE_MAX_FILENAME] = '\0';
            }

            m_track_count++;

        } else if (str_equal_nocase(token, "INDEX", 5)) {
            // INDEX nn MM:SS:FF
            if (!current_track) {
                pos = next_line(pos);
                continue;
            }

            pos = extract_token(pos, token, sizeof(token), false);
            int index_num = 0;
            for (const char *p = token; *p >= '0' && *p <= '9'; p++) {
                index_num = index_num * 10 + (*p - '0');
            }

            pos = extract_token(pos, token, sizeof(token), false);
            uint32_t frames = parse_msf(token);

            if (index_num < CUE_MAX_INDEXES) {
                current_track->index[index_num] = frames;
                if (index_num >= current_track->index_count) {
                    current_track->index_count = index_num + 1;
                }
            }

            if (index_num == 0) {
                current_track->has_index0 = true;
            }

        } else if (str_equal_nocase(token, "PREGAP", 6)) {
            // PREGAP MM:SS:FF (silence not stored in file)
            if (!current_track) {
                pos = next_line(pos);
                continue;
            }

            pos = extract_token(pos, token, sizeof(token), false);
            uint32_t frames = parse_msf(token);
            current_track->info.unstored_pregap_length = frames;
            cumulative_pregap += frames;

        } else if (str_equal_nocase(token, "POSTGAP", 7)) {
            // POSTGAP MM:SS:FF
            if (!current_track) {
                pos = next_line(pos);
                continue;
            }

            pos = extract_token(pos, token, sizeof(token), false);
            current_track->postgap_length = parse_msf(token);

        } else if (str_equal_nocase(token, "FLAGS", 5)) {
            // FLAGS PRE DCP 4CH SCMS
            if (!current_track) {
                pos = next_line(pos);
                continue;
            }

            // Read flags until end of line
            const char *line_end = find_line_end(pos);
            char flags_str[64];
            int len = line_end - pos;
            if (len > (int)sizeof(flags_str) - 1) {
                len = sizeof(flags_str) - 1;
            }
            memcpy(flags_str, pos, len);
            flags_str[len] = '\0';
            current_track->info.flags = parse_flags(flags_str);
            pos = line_end;

        } else if (str_equal_nocase(token, "ISRC", 4)) {
            // ISRC ABCDE1234567 (12 characters)
            if (!current_track) {
                pos = next_line(pos);
                continue;
            }
            pos = extract_token(pos, token, sizeof(token), false);
            safe_strcpy(current_track->info.isrc, token, CUE_ISRC_LENGTH + 1);

        } else if (str_equal_nocase(token, "CATALOG", 7)) {
            // CATALOG 1234567890123 (13 digits, disc-level)
            pos = extract_token(pos, token, sizeof(token), false);
            safe_strcpy(m_disc_info.catalog, token, CUE_CATALOG_LENGTH + 1);

        } else if (str_equal_nocase(token, "CDTEXTFILE", 10)) {
            // CDTEXTFILE "filename.cdt"
            pos = extract_token(pos, filename, sizeof(filename), false);
            safe_strcpy(m_disc_info.cdtextfile, filename, CUE_MAX_FILENAME + 1);

        } else if (str_equal_nocase(token, "TITLE", 5)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.title, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.title, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "PERFORMER", 9)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.performer, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.performer, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "SONGWRITER", 10)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.songwriter, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.songwriter, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "COMPOSER", 8)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.composer, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.composer, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "ARRANGER", 8)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.arranger, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.arranger, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "MESSAGE", 7)) {
            pos = extract_token(pos, token, sizeof(token), false);
            if (current_track) {
                safe_strcpy(current_track->info.cdtext.message, token, CUE_MAX_CDTEXT + 1);
            } else {
                safe_strcpy(m_disc_info.cdtext.message, token, CUE_MAX_CDTEXT + 1);
            }

        } else if (str_equal_nocase(token, "REM", 3)) {
            // REM comments - parse the rest of the line
            if (current_track) {
                parse_rem_field(pos, &current_track->info.rem);
            } else {
                parse_rem_field(pos, &m_disc_info.rem);
            }
        }

        pos = next_line(pos);
    }

    // Calculate LBA positions for all tracks
    recalculate_lba_positions();
    m_parsed = true;
}

void CUEParser::recalculate_lba_positions() {
    uint32_t file_start_lba = 0;
    int prev_file_index = -1;

    for (int i = 0; i < m_track_count; i++) {
        CUEParsedTrack *track = &m_tracks[i];
        CUETrackInfo *info = &track->info;

        // Check if we switched to a new file
        if (info->file_index != prev_file_index) {
            if (prev_file_index >= 0 && prev_file_index < m_file_count) {
                // Add previous file's length to LBA offset
                uint64_t prev_size = m_files[prev_file_index].size;
                if (prev_size > 0 && i > 0) {
                    uint32_t prev_sector_size = m_tracks[i - 1].info.sector_length;
                    if (prev_sector_size > 0) {
                        uint32_t prev_file_frames = prev_size / prev_sector_size;
                        file_start_lba += prev_file_frames;
                    }
                }
            }
            prev_file_index = info->file_index;
            info->file_start = file_start_lba;
        } else {
            info->file_start = file_start_lba;
        }

        // Get INDEX 01 position (main track data)
        uint32_t index01_frames = 0;
        if (track->index_count > 1) {
            index01_frames = track->index[1];
        } else if (track->index_count > 0) {
            index01_frames = track->index[0];
        }

        // Calculate data_start (INDEX 01 position in absolute LBA)
        info->data_start = file_start_lba + index01_frames + info->cumulative_offset;

        // Calculate track_start (INDEX 00 if present, else INDEX 01)
        if (track->has_index0) {
            info->track_start = file_start_lba + track->index[0] + info->cumulative_offset;
            // Adjust data_start to account for stored pregap
            info->data_start = file_start_lba + index01_frames + info->cumulative_offset;
        } else {
            // No INDEX 00, track starts at INDEX 01
            // If there's an unstored pregap, track_start is before data_start
            info->track_start = info->data_start;
            if (info->unstored_pregap_length > 0) {
                info->data_start = info->track_start + info->unstored_pregap_length;
            }
        }

        // Calculate file offset (byte position in the bin file)
        info->file_offset = (uint64_t)index01_frames * info->sector_length;
    }
}

const char *CUEParser::extract_token(const char *line, char *token, int token_size, bool to_upper) {
    token[0] = '\0';
    line = skip_whitespace(line);

    if (*line == '\0' || *line == '\n' || *line == '\r') {
        return line;
    }

    int len = 0;

    // Handle quoted strings
    if (*line == '"') {
        line++;
        while (*line && *line != '"' && *line != '\n' && *line != '\r') {
            if (len < token_size - 1) {
                token[len++] = *line;
            }
            line++;
        }
        if (*line == '"') {
            line++;
        }
    } else {
        // Unquoted token - read until whitespace
        while (*line && !is_whitespace(*line)) {
            if (len < token_size - 1) {
                char c = *line;
                if (to_upper && c >= 'a' && c <= 'z') {
                    c = c - 'a' + 'A';
                }
                token[len++] = c;
            }
            line++;
        }
    }

    token[len] = '\0';
    return line;
}

uint32_t CUEParser::parse_msf(const char *str) {
    uint32_t minutes = 0, seconds = 0, frames = 0;

    // Parse minutes
    while (*str >= '0' && *str <= '9') {
        minutes = minutes * 10 + (*str - '0');
        str++;
    }
    if (*str == ':') str++;

    // Parse seconds
    while (*str >= '0' && *str <= '9') {
        seconds = seconds * 10 + (*str - '0');
        str++;
    }
    if (*str == ':') str++;

    // Parse frames
    while (*str >= '0' && *str <= '9') {
        frames = frames * 10 + (*str - '0');
        str++;
    }

    return frames + CD_FRAMES_PER_SECOND * (seconds + CD_SECONDS_PER_MINUTE * minutes);
}

CUEFileMode CUEParser::parse_file_mode(const char *str) {
    if (str_equal_nocase(str, "BINARY", 6)) return CUEFile_BINARY;
    if (str_equal_nocase(str, "MOTOROLA", 8)) return CUEFile_MOTOROLA;
    if (str_equal_nocase(str, "AIFF", 4)) return CUEFile_AIFF;
    if (str_equal_nocase(str, "WAVE", 4)) return CUEFile_WAVE;
    if (str_equal_nocase(str, "WAV", 3)) return CUEFile_WAVE;
    if (str_equal_nocase(str, "MP3", 3)) return CUEFile_MP3;
    return CUEFile_BINARY;
}

CUETrackMode CUEParser::parse_track_mode(const char *str, uint32_t *sector_size) {
    CUETrackMode mode = CUETrack_AUDIO;
    uint32_t size = 2352;

    if (str_equal_nocase(str, "AUDIO", 5)) {
        mode = CUETrack_AUDIO;
        size = 2352;
    } else if (str_equal_nocase(str, "CDG", 3)) {
        mode = CUETrack_CDG;
        size = 2448;
    } else if (str_equal_nocase(str, "MODE1/2048", 10)) {
        mode = CUETrack_MODE1_2048;
        size = 2048;
    } else if (str_equal_nocase(str, "MODE1/2352", 10)) {
        mode = CUETrack_MODE1_2352;
        size = 2352;
    } else if (str_equal_nocase(str, "MODE2/2048", 10)) {
        mode = CUETrack_MODE2_2048;
        size = 2048;
    } else if (str_equal_nocase(str, "MODE2/2324", 10)) {
        mode = CUETrack_MODE2_2324;
        size = 2324;
    } else if (str_equal_nocase(str, "MODE2/2336", 10)) {
        mode = CUETrack_MODE2_2336;
        size = 2336;
    } else if (str_equal_nocase(str, "MODE2/2352", 10)) {
        mode = CUETrack_MODE2_2352;
        size = 2352;
    } else if (str_equal_nocase(str, "CDI/2336", 8)) {
        mode = CUETrack_CDI_2336;
        size = 2336;
    } else if (str_equal_nocase(str, "CDI/2352", 8)) {
        mode = CUETrack_CDI_2352;
        size = 2352;
    }

    if (sector_size) {
        *sector_size = size;
    }
    return mode;
}

uint8_t CUEParser::parse_flags(const char *str) {
    uint8_t flags = CUEFlag_NONE;

    while (*str) {
        str = skip_whitespace(str);
        if (*str == '\0') break;

        if (str_equal_nocase(str, "PRE", 3)) {
            flags |= CUEFlag_PRE;
            str += 3;
        } else if (str_equal_nocase(str, "DCP", 3)) {
            flags |= CUEFlag_DCP;
            str += 3;
        } else if (str_equal_nocase(str, "4CH", 3)) {
            flags |= CUEFlag_4CH;
            str += 3;
        } else if (str_equal_nocase(str, "SCMS", 4)) {
            flags |= CUEFlag_SCMS;
            str += 4;
        } else {
            // Skip unknown token
            while (*str && !is_whitespace(*str)) str++;
        }
    }

    return flags;
}

uint32_t CUEParser::get_sector_size(CUEFileMode file_mode, CUETrackMode track_mode) {
    if (file_mode != CUEFile_BINARY && file_mode != CUEFile_MOTOROLA) {
        return 0;  // Audio files don't have fixed sector sizes
    }

    switch (track_mode) {
        case CUETrack_AUDIO:      return 2352;
        case CUETrack_CDG:        return 2448;
        case CUETrack_MODE1_2048: return 2048;
        case CUETrack_MODE1_2352: return 2352;
        case CUETrack_MODE2_2048: return 2048;
        case CUETrack_MODE2_2324: return 2324;
        case CUETrack_MODE2_2336: return 2336;
        case CUETrack_MODE2_2352: return 2352;
        case CUETrack_CDI_2336:   return 2336;
        case CUETrack_CDI_2352:   return 2352;
        default:                  return 2048;
    }
}

const char *CUEParser::skip_whitespace(const char *p) {
    while (*p && is_whitespace(*p) && *p != '\n' && *p != '\r') {
        p++;
    }
    return p;
}

const char *CUEParser::find_line_end(const char *p) {
    while (*p && *p != '\n' && *p != '\r') {
        p++;
    }
    return p;
}

const char *CUEParser::next_line(const char *p) {
    // Skip to end of current line
    while (*p && *p != '\n' && *p != '\r') {
        p++;
    }
    // Skip line ending characters
    while (*p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

bool CUEParser::is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

bool CUEParser::str_equal_nocase(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        char ca = a[i];
        char cb = b[i];

        // Convert to uppercase
        if (ca >= 'a' && ca <= 'z') ca = ca - 'a' + 'A';
        if (cb >= 'a' && cb <= 'z') cb = cb - 'a' + 'A';

        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
    return true;
}

void CUEParser::safe_strcpy(char *dest, const char *src, int dest_size) {
    if (dest_size <= 0) return;
    int i = 0;
    while (i < dest_size - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void CUEParser::parse_cdtext_field(const char *keyword, const char *value, CUECDText *cdtext) {
    if (str_equal_nocase(keyword, "TITLE", 5)) {
        safe_strcpy(cdtext->title, value, CUE_MAX_CDTEXT + 1);
    } else if (str_equal_nocase(keyword, "PERFORMER", 9)) {
        safe_strcpy(cdtext->performer, value, CUE_MAX_CDTEXT + 1);
    } else if (str_equal_nocase(keyword, "SONGWRITER", 10)) {
        safe_strcpy(cdtext->songwriter, value, CUE_MAX_CDTEXT + 1);
    } else if (str_equal_nocase(keyword, "COMPOSER", 8)) {
        safe_strcpy(cdtext->composer, value, CUE_MAX_CDTEXT + 1);
    } else if (str_equal_nocase(keyword, "ARRANGER", 8)) {
        safe_strcpy(cdtext->arranger, value, CUE_MAX_CDTEXT + 1);
    } else if (str_equal_nocase(keyword, "MESSAGE", 7)) {
        safe_strcpy(cdtext->message, value, CUE_MAX_CDTEXT + 1);
    }
}

void CUEParser::parse_rem_field(const char *line, CUERem *rem) {
    char keyword[64];
    char value[CUE_MAX_REM + 1];

    // Extract the REM sub-keyword
    line = skip_whitespace(line);
    line = extract_token(line, keyword, sizeof(keyword), true);

    // Extract the value (rest of line or quoted string)
    line = extract_token(line, value, sizeof(value), false);

    if (str_equal_nocase(keyword, "DATE", 4)) {
        safe_strcpy(rem->date, value, CUE_MAX_REM + 1);
    } else if (str_equal_nocase(keyword, "GENRE", 5)) {
        safe_strcpy(rem->genre, value, CUE_MAX_REM + 1);
    } else if (str_equal_nocase(keyword, "DISCID", 6)) {
        safe_strcpy(rem->discid, value, CUE_MAX_REM + 1);
    } else if (str_equal_nocase(keyword, "COMMENT", 7)) {
        safe_strcpy(rem->comment, value, CUE_MAX_REM + 1);
    } else if (str_equal_nocase(keyword, "DISCNUMBER", 10)) {
        rem->disc_number = 0;
        for (const char *p = value; *p >= '0' && *p <= '9'; p++) {
            rem->disc_number = rem->disc_number * 10 + (*p - '0');
        }
    } else if (str_equal_nocase(keyword, "TOTALDISCS", 10)) {
        rem->total_discs = 0;
        for (const char *p = value; *p >= '0' && *p <= '9'; p++) {
            rem->total_discs = rem->total_discs * 10 + (*p - '0');
        }
    } else if (str_equal_nocase(keyword, "REPLAYGAIN_ALBUM_GAIN", 21)) {
        safe_strcpy(rem->replaygain_album_gain, value, sizeof(rem->replaygain_album_gain));
    } else if (str_equal_nocase(keyword, "REPLAYGAIN_ALBUM_PEAK", 21)) {
        safe_strcpy(rem->replaygain_album_peak, value, sizeof(rem->replaygain_album_peak));
    } else if (str_equal_nocase(keyword, "REPLAYGAIN_TRACK_GAIN", 21)) {
        safe_strcpy(rem->replaygain_track_gain, value, sizeof(rem->replaygain_track_gain));
    } else if (str_equal_nocase(keyword, "REPLAYGAIN_TRACK_PEAK", 21)) {
        safe_strcpy(rem->replaygain_track_peak, value, sizeof(rem->replaygain_track_peak));
    }
    // Unknown REM fields are silently ignored
}
