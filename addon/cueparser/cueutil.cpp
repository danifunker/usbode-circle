/*
 * Standalone CUE sheet utilities built on CUEParser.
 *
 * Copyright (c) 2026 USBODE project
 */

#include "cueutil.h"

bool CueFindTrackForLBA(const char *cue_sheet, uint32_t lba, CUETrackInfo *out) {
    if (cue_sheet == nullptr || out == nullptr) {
        return false;
    }

    CUEParser parser(cue_sheet);
    const CUETrackInfo *trackInfo;
    bool found = false;

    while ((trackInfo = parser.next_track()) != nullptr) {
        if (found && lba < trackInfo->track_start) {
            // The previous track owns this LBA.
            return true;
        }
        *out = *trackInfo;
        found = true;
    }

    // LBA is in (or beyond) the last track.
    return found;
}

uint64_t CueLBAToByteOffset(const char *cue_sheet, uint32_t lba) {
    CUETrackInfo track;
    if (!CueFindTrackForLBA(cue_sheet, lba, &track)) {
        return (uint64_t)lba * 2352ULL;
    }

    int64_t rel = (int64_t)lba - (int64_t)track.data_start;
    if (rel < 0 && track.unstored_pregap_length > 0) {
        // Unstored pregap frames have no bytes in the file; clamp to the
        // start of the track's data. (A stored INDEX 00 pregap keeps
        // rel < 0, which correctly addresses the pregap bytes preceding
        // data_start in the file.)
        rel = 0;
    }

    int64_t offset = (int64_t)track.file_offset + rel * (int64_t)track.sector_length;
    if (offset < 0) {
        // LBA below the first track's start; clamp to the file start.
        offset = 0;
    }

    return (uint64_t)offset;
}
