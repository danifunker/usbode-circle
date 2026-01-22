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

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef CUE_MAX_FILENAME
#define CUE_MAX_FILENAME 256
#endif

#ifndef CUE_MAX_TRACKS
#define CUE_MAX_TRACKS 99
#endif

#ifndef CUE_MAX_INDEXES
#define CUE_MAX_INDEXES 100
#endif

#ifndef CUE_MAX_CDTEXT
#define CUE_MAX_CDTEXT 256
#endif

#ifndef CUE_MAX_REM
#define CUE_MAX_REM 256
#endif

// ISRC is exactly 12 characters
#define CUE_ISRC_LENGTH 12

// CATALOG (UPC/EAN) is exactly 13 digits
#define CUE_CATALOG_LENGTH 13

// File format types (FILE directive)
enum CUEFileMode {
    CUEFile_BINARY = 0,
    CUEFile_MOTOROLA,
    CUEFile_MP3,
    CUEFile_WAVE,
    CUEFile_AIFF,
};

// Track mode types (TRACK directive)
enum CUETrackMode {
    CUETrack_AUDIO = 0,
    CUETrack_CDG,
    CUETrack_MODE1_2048,
    CUETrack_MODE1_2352,
    CUETrack_MODE2_2048,
    CUETrack_MODE2_2324,
    CUETrack_MODE2_2336,
    CUETrack_MODE2_2352,
    CUETrack_CDI_2336,
    CUETrack_CDI_2352,
};

// Track flags (FLAGS directive)
enum CUETrackFlags {
    CUEFlag_NONE = 0x00,
    CUEFlag_PRE  = 0x01,    // Pre-emphasis
    CUEFlag_DCP  = 0x02,    // Digital copy permitted
    CUEFlag_4CH  = 0x04,    // Four channel audio
    CUEFlag_SCMS = 0x08,    // Serial copy management system
};

// CD-TEXT fields (per-disc or per-track)
struct CUECDText {
    char title[CUE_MAX_CDTEXT + 1];
    char performer[CUE_MAX_CDTEXT + 1];
    char songwriter[CUE_MAX_CDTEXT + 1];
    char composer[CUE_MAX_CDTEXT + 1];
    char arranger[CUE_MAX_CDTEXT + 1];
    char message[CUE_MAX_CDTEXT + 1];
};

// REM (remark) fields
struct CUERem {
    char date[CUE_MAX_REM + 1];             // REM DATE
    char genre[CUE_MAX_REM + 1];            // REM GENRE
    char discid[CUE_MAX_REM + 1];           // REM DISCID
    char comment[CUE_MAX_REM + 1];          // REM COMMENT
    int disc_number;                         // REM DISCNUMBER
    int total_discs;                         // REM TOTALDISCS
    // ReplayGain values (stored as strings to preserve precision)
    char replaygain_album_gain[32];
    char replaygain_album_peak[32];
    char replaygain_track_gain[32];
    char replaygain_track_peak[32];
};

// Information about a single track
struct CUETrackInfo {
    // Source file name and file type
    char filename[CUE_MAX_FILENAME + 1];
    int file_index;             // Which FILE directive (0-based)
    CUEFileMode file_mode;
    uint64_t file_offset;       // Byte offset into the bin file for track data

    // Track metadata
    int track_number;           // Track number (1-99)
    CUETrackMode track_mode;
    uint32_t sector_length;     // Bytes per sector
    uint8_t flags;              // CUETrackFlags bitmask

    // ISRC code (12 characters)
    char isrc[CUE_ISRC_LENGTH + 1];

    // Pregap handling
    uint32_t unstored_pregap_length;  // PREGAP frames (not in file)
    uint32_t cumulative_offset;       // Running total of unstored pregaps

    // LBA positions
    uint32_t file_start;        // LBA where this file begins
    uint32_t data_start;        // LBA of INDEX 01
    uint32_t track_start;       // LBA of INDEX 00 (or INDEX 01 if no 00)

    // Per-track CD-TEXT
    CUECDText cdtext;

    // Per-track REM (ReplayGain)
    CUERem rem;
};

// Internal structure for tracking parsed files
struct CUEFileEntry {
    char filename[CUE_MAX_FILENAME + 1];
    CUEFileMode mode;
    uint64_t size;              // Set via set_file_size()
};

// Disc-level metadata
struct CUEDiscInfo {
    char catalog[CUE_CATALOG_LENGTH + 1];   // UPC/EAN barcode
    char cdtextfile[CUE_MAX_FILENAME + 1];  // CDTEXTFILE reference
    CUECDText cdtext;                        // Disc-level CD-TEXT
    CUERem rem;                              // Disc-level REM comments
};

// Internal structure for a fully parsed track
struct CUEParsedTrack {
    CUETrackInfo info;
    uint32_t index[CUE_MAX_INDEXES];  // All index points in frames
    uint8_t index_count;
    bool has_index0;
    uint32_t postgap_length;    // POSTGAP frames
};

class CUEParser {
public:
    CUEParser();

    // Initialize parser with CUE sheet content.
    // The string must remain valid for the lifetime of this object.
    CUEParser(const char *cue_sheet);

    // Restart iteration from the first track
    void restart();

    // Get next track info. Returns nullptr when no more tracks.
    // The returned pointer is valid until next call to next_track().
    const CUETrackInfo *next_track();

    // Get next track, providing previous file's size for multi-bin support.
    // When switching to a new FILE, prev_file_size is used to calculate
    // the correct LBA continuation.
    const CUETrackInfo *next_track(uint64_t prev_file_size);

    // Get total number of tracks (available after parsing)
    int get_track_count() const;

    // Get track by number (1-based). Returns nullptr if not found.
    const CUETrackInfo *get_track(int track_number);

    // Set file size for a specific file index (for multi-bin LBA calculation)
    void set_file_size(int file_index, uint64_t size);

    // Get disc-level metadata
    const CUEDiscInfo *get_disc_info() const;

    // Convenience accessors for common disc metadata
    const char *get_catalog() const;
    const char *get_cdtextfile() const;
    const CUECDText *get_disc_cdtext() const;
    const CUERem *get_disc_rem() const;

protected:
    // Source CUE sheet
    const char *m_cue_sheet;

    // Disc-level metadata
    CUEDiscInfo m_disc_info;

    // Parsed track storage
    CUEParsedTrack m_tracks[CUE_MAX_TRACKS];
    int m_track_count;

    // File entries for multi-bin support
    CUEFileEntry m_files[CUE_MAX_TRACKS];
    int m_file_count;

    // Iterator state
    int m_current_track;
    CUETrackInfo m_iter_info;

    // Parse state
    bool m_parsed;

    // Parse the entire CUE sheet
    void parse_cue_sheet();

    // Recalculate LBA positions after file sizes are known
    void recalculate_lba_positions();

    // Token extraction (86Box-inspired approach)
    // Extracts next token from line, handles quoted strings
    // Returns pointer past the extracted token
    const char *extract_token(const char *line, char *token, int token_size, bool to_upper);

    // Parse MSF time string (MM:SS:FF) to frame count
    uint32_t parse_msf(const char *str);

    // Parse file mode string to enum
    CUEFileMode parse_file_mode(const char *str);

    // Parse track mode string to enum and sector size
    CUETrackMode parse_track_mode(const char *str, uint32_t *sector_size);

    // Parse flags string
    uint8_t parse_flags(const char *str);

    // Get sector size for a track mode
    uint32_t get_sector_size(CUEFileMode file_mode, CUETrackMode track_mode);

    // Skip whitespace, return pointer to first non-whitespace
    const char *skip_whitespace(const char *p);

    // Find end of current line
    const char *find_line_end(const char *p);

    // Advance to next line
    const char *next_line(const char *p);

    // Check if character is whitespace
    static bool is_whitespace(char c);

    // Case-insensitive string comparison (limited length)
    static bool str_equal_nocase(const char *a, const char *b, int len);

    // Copy string to fixed-size buffer safely
    static void safe_strcpy(char *dest, const char *src, int dest_size);

    // Parse CD-TEXT directive and store in appropriate cdtext struct
    void parse_cdtext_field(const char *keyword, const char *value, CUECDText *cdtext);

    // Parse REM directive
    void parse_rem_field(const char *line, CUERem *rem);
};
