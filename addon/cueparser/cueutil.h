/*
 * Standalone CUE sheet utilities built on CUEParser.
 *
 * Copyright (c) 2026 USBODE project
 *
 * Pure functions with no Circle/FatFs dependencies so they can be reused
 * from any addon (and unit-tested on a host machine).
 */

#pragma once

#include <stdint.h>

#include "cueparser.h"

// Find the track containing the given LBA, using the same containment rule
// as CDUtils::GetTrackInfoForLBA (a track owns every LBA from its
// track_start up to the next track's track_start; the last track owns
// everything after it). Returns false if the cue sheet has no tracks.
bool CueFindTrackForLBA(const char *cue_sheet, uint32_t lba, CUETrackInfo *out);

// Translate a CD frame LBA to its byte offset within the single BIN file
// described by the cue sheet, honouring per-track sector sizes (e.g. a
// MODE1/2048 data track followed by 2352-byte audio tracks). LBAs inside an
// unstored PREGAP have no bytes in the file and clamp to the track's data
// start. Falls back to lba * 2352 if the cue sheet is null or has no tracks.
uint64_t CueLBAToByteOffset(const char *cue_sheet, uint32_t lba);

// Which file holds an LBA, and where in that file.
struct CueFileLocation {
    int file_index;  // 0-based, in the order the FILE lines appear
    char filename[CUE_MAX_FILENAME + 1];
    uint64_t offset;
};

// Number of FILE lines, and the name of the nth (0-based).
int CueCountFiles(const char *cue_sheet);
bool CueGetFileName(const char *cue_sheet, int index, char *out, size_t out_size);

// file_sizes gives every FILE's size in cue order; single-FILE sheets pass none.
bool CueResolveLBA(const char *cue_sheet, uint32_t lba,
                   const uint64_t *file_sizes, int file_count,
                   CueFileLocation *out);
