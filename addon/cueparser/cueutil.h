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

// True if the cue sheet references more than one data file - a "split track"
// rip, where each track is its own .bin.
//
// USBODE cannot mount one. The loader opens the single file named after the
// cue and never looks at the FILE lines, and the parser cannot place tracks
// from a later file without knowing how long the earlier ones are. The result
// is a disc whose TOC is wrong rather than a disc that fails to load, so this
// exists to let the loader refuse the image outright.
//
// Uses the real parser rather than scanning for the word FILE, so quoting,
// comments and REM lines are treated exactly as they will be when the sheet is
// actually read.
bool CueHasMultipleFiles(const char *cue_sheet);
