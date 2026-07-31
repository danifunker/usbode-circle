/*
 * Standalone CUE sheet utilities built on CUEParser.
 *
 * Copyright (c) 2026 USBODE project
 */

#include "cueutil.h"

#include <string.h>

namespace {

// Use the size of the file ending at this FILE boundary.
const CUETrackInfo *NextTrackSized(CUEParser &parser, int prev_file_index,
                                   const uint64_t *file_sizes, int file_count) {
    uint64_t prev_size = 0;
    if (file_sizes != nullptr && prev_file_index >= 1 && prev_file_index <= file_count) {
        prev_size = file_sizes[prev_file_index - 1];
    }
    return parser.next_track(prev_size);
}

void CopyName(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

}  // namespace

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

int CueCountFiles(const char *cue_sheet) {
    if (cue_sheet == nullptr) {
        return 0;
    }

    CUEParser parser(cue_sheet);
    const CUETrackInfo *trackInfo;
    int highest = 0;

    // file_index is 1-based, so the largest one is the file count.
    while ((trackInfo = parser.next_track()) != nullptr) {
        if (trackInfo->file_index > highest) {
            highest = trackInfo->file_index;
        }
    }

    return highest;
}

bool CueGetFileName(const char *cue_sheet, int index, char *out, size_t out_size) {
    if (cue_sheet == nullptr || out == nullptr || out_size == 0 || index < 0) {
        return false;
    }

    CUEParser parser(cue_sheet);
    const CUETrackInfo *trackInfo;

    while ((trackInfo = parser.next_track()) != nullptr) {
        if (trackInfo->file_index == index + 1) {
            CopyName(out, out_size, trackInfo->filename);
            return true;
        }
    }

    return false;
}

bool CueResolveLBA(const char *cue_sheet, uint32_t lba,
                   const uint64_t *file_sizes, int file_count,
                   CueFileLocation *out) {
    if (cue_sheet == nullptr || out == nullptr) {
        return false;
    }

    // A short size table would place later files at plausible but wrong addresses.
    int nFiles = CueCountFiles(cue_sheet);
    if (nFiles > 1 && (file_sizes == nullptr || file_count < nFiles)) {
        return false;
    }

    CUEParser parser(cue_sheet);
    const CUETrackInfo *trackInfo;
    CUETrackInfo track;
    bool found = false;
    int prev_file_index = 0;

    while ((trackInfo = NextTrackSized(parser, prev_file_index, file_sizes, file_count)) != nullptr) {
        if (found && lba < trackInfo->track_start) {
            break;
        }
        track = *trackInfo;
        found = true;
        prev_file_index = trackInfo->file_index;
    }

    if (!found) {
        return false;
    }

    int64_t rel = (int64_t)lba - (int64_t)track.data_start;
    if (rel < 0 && track.unstored_pregap_length > 0) {
        rel = 0;
    }

    int64_t offset = (int64_t)track.file_offset + rel * (int64_t)track.sector_length;
    if (offset < 0) {
        offset = 0;
    }

    out->file_index = track.file_index > 0 ? track.file_index - 1 : 0;
    CopyName(out->filename, sizeof(out->filename), track.filename);
    out->offset = (uint64_t)offset;
    return true;
}
