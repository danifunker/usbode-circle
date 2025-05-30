/*
 * Simple CUE sheet parser suitable for embedded systems.
 *
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

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef CUE_MAX_FILENAME
#define CUE_MAX_FILENAME 256
#endif

enum CUEFileMode {
    CUEFile_BINARY = 0,
    CUEFile_MOTOROLA,
    CUEFile_MP3,
    CUEFile_WAVE,
    CUEFile_AIFF,
};

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

struct CUETrackInfo {
    // Source file name and file type, and offset to start of track data in bytes.
    char filename[CUE_MAX_FILENAME + 1];
    int file_index;
    CUEFileMode file_mode;
    uint64_t file_offset;  // corresponds to data_start below

    // Track number and mode in CD format
    int track_number;
    CUETrackMode track_mode;

    // Sector length for this track in bytes, assuming BINARY or MOTOROLA file modes.
    uint32_t sector_length;

    // The CD frames of PREGAP time at the start of this track, or 0 if none are present.
    // These frames of silence are not stored in the underlying data file.
    uint32_t unstored_pregap_length;

    // The cumulative lba offset of unstored data
    uint32_t cumulative_offset;

    // LBA start position of this file
    uint32_t file_start;

    // LBA start position of the data area (INDEX 01) of this track (in CD frames)
    uint32_t data_start;

    // LBA for the beginning of the track, which will be INDEX 00 if that is present.
    // If there is unstored PREGAP, it's added between track_start and data_start.
    // Otherwise this will be INDEX 01 matching data_start above.
    uint32_t track_start;
};

class CUEParser {
   public:
    CUEParser();

    // Initialize the class to parse data from string.
    // The string must remain valid for the lifetime of this object.
    CUEParser(const char *cue_sheet);

    // Restart parsing from beginning of file
    void restart();

    // Get information for next track.
    // Returns nullptr when there are no more tracks.
    // The returned pointer remains valid until next call to next_track()
    // or destruction of this object.
    const CUETrackInfo *next_track();

    // Same as next_track(), but takes the file size into account when
    // switching files. This is necessary for getting the correct track
    // lengths when the .cue file references multiple .bin files.
    const CUETrackInfo *next_track(uint64_t prev_file_size);

   protected:
    const char *m_cue_sheet;
    const char *m_parse_pos;
    CUETrackInfo m_track_info;

    // Skip any whitespace at beginning of line.
    // Returns false if at end of string.
    bool start_line();

    // Advance parser to next line
    void next_line();

    // Skip spaces in string, return pointer to first non-space character
    const char *skip_space(const char *p) const;

    // Read text starting with " and ending with next "
    // Returns pointer to character after ending quote.
    const char *read_quoted(const char *src, char *dest, int dest_size);

    // Parse time from MM:SS:FF format to frame number
    uint32_t parse_time(const char *src);

    // Parse file mode into enum
    CUEFileMode parse_file_mode(const char *src);

    // Parse track mode into enum
    CUETrackMode parse_track_mode(const char *src);

    // Get sector length in file from track mode
    uint32_t get_sector_length(CUEFileMode filemode, CUETrackMode trackmode);

    // Remove './' or '.\' from the beginning of the filename as it is not recogized by the SDFat library
    void remove_dot_slash(char *filename, size_t length);
};
