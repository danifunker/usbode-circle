//
// Host-side tests for the bundled CUEParser.
//
// Build & run:  cc -o cueparser_test cueparser_test.cpp ../addon/cueparser/cueparser.cpp -lstdc++ && ./cueparser_test
//
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../addon/cueparser/cueparser.h"

static int failures = 0;

#define CHECK_EQ(actual, expected, what)                                            \
    do {                                                                            \
        unsigned long long a = (unsigned long long)(actual);                        \
        unsigned long long e = (unsigned long long)(expected);                      \
        if (a != e) {                                                               \
            printf("FAIL %s:%d %s: got %llu, expected %llu\n", __func__, __LINE__, \
                   what, a, e);                                                     \
            failures++;                                                             \
        }                                                                           \
    } while (0)

// Upstream header example: PREGAP-style cue, mixed sector sizes, single file.
static void test_pregap_style_mixed_modes() {
    const char *cue =
        "FILE \"foo bar.bin\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 02:47:20\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 00 07:55:58\n"
        "    INDEX 01 07:55:65\n";

    CUEParser p(cue);
    const CUETrackInfo *t;

    t = p.next_track();
    CHECK_EQ(t->track_number, 1, "t1 number");
    CHECK_EQ(t->track_start, 0, "t1 track_start");
    CHECK_EQ(t->data_start, 0, "t1 data_start");
    CHECK_EQ(t->file_offset, 0, "t1 file_offset");
    CHECK_EQ(t->sector_length, 2048, "t1 sector_length");

    t = p.next_track();
    CHECK_EQ(t->track_number, 2, "t2 number");
    // INDEX 01 02:47:20 = 12545 file frames; 150-frame unstored pregap
    CHECK_EQ(t->track_start, 12545, "t2 track_start");
    CHECK_EQ(t->data_start, 12695, "t2 data_start");
    CHECK_EQ(t->file_offset, 12545ULL * 2048, "t2 file_offset");

    t = p.next_track();
    CHECK_EQ(t->track_number, 3, "t3 number");
    // INDEX 00 07:55:58 = 35683 file frames, INDEX 01 = 35690; +150 cumulative
    CHECK_EQ(t->track_start, 35833, "t3 track_start");
    CHECK_EQ(t->data_start, 35840, "t3 data_start");
    CHECK_EQ(t->file_offset, 12545ULL * 2048 + (35690ULL - 12545) * 2352, "t3 file_offset");

    CHECK_EQ((unsigned long long)p.next_track(), 0, "no t4");
}

// Single file with INDEX 00 chains (the file_offset accumulator bug):
// every track after a track with a stored pregap used to be offset too far.
static void test_index00_chain_single_file() {
    const char *cue =
        "FILE \"disc.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:13:25\n"   // frame 1000
        "    INDEX 01 00:15:25\n"   // frame 1150
        "  TRACK 03 AUDIO\n"
        "    INDEX 00 00:26:50\n"   // frame 2000
        "    INDEX 01 00:28:50\n";  // frame 2150

    CUEParser p(cue);
    const CUETrackInfo *t;

    t = p.next_track();
    CHECK_EQ(t->file_offset, 0, "t1 file_offset");

    t = p.next_track();
    CHECK_EQ(t->track_start, 1000, "t2 track_start");
    CHECK_EQ(t->data_start, 1150, "t2 data_start");
    CHECK_EQ(t->file_offset, 1150ULL * 2352, "t2 file_offset");

    t = p.next_track();
    CHECK_EQ(t->track_start, 2000, "t3 track_start");
    CHECK_EQ(t->data_start, 2150, "t3 data_start");
    CHECK_EQ(t->file_offset, 2150ULL * 2352, "t3 file_offset");  // was 2300*2352 before fix
}

// Multi-file with PREGAP directives (file_start used to double-count
// cumulative unstored pregaps).
static void test_multifile_pregap() {
    const char *cue =
        "FILE \"t1.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"t2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"t3.bin\" BINARY\n"
        "  TRACK 03 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 00:00:00\n";

    CUEParser p(cue);
    const CUETrackInfo *t;

    t = p.next_track(0);
    CHECK_EQ(t->data_start, 0, "t1 data_start");

    t = p.next_track(1000ULL * 2352);  // size of t1.bin
    CHECK_EQ(t->track_start, 1000, "t2 track_start");
    CHECK_EQ(t->data_start, 1150, "t2 data_start");
    CHECK_EQ(t->file_offset, 0, "t2 file_offset");

    t = p.next_track(500ULL * 2352);  // size of t2.bin
    CHECK_EQ(t->track_start, 1650, "t3 track_start");
    CHECK_EQ(t->data_start, 1800, "t3 data_start");  // was 1950 before fix
    CHECK_EQ(t->file_offset, 0, "t3 file_offset");
}

// Redump-style multi-bin CD-EXTRA, shaped like the Akumajou Dracula MIDI
// Collection test disc (sizes from the real rip for the first three and the
// last two tracks; intermediate tracks elided — the parser only ever needs
// the previous file's size).
static void test_redump_multibin_sessions() {
    const char *cue =
        "REM SESSION 01\n"
        "FILE \"Track 01.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"Track 02.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:00:00\n"
        "    INDEX 01 00:01:42\n"
        "FILE \"Track 03.bin\" BINARY\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 00 00:00:00\n"
        "    INDEX 01 00:02:15\n"
        "REM SESSION 02\n"
        "FILE \"Track 04.bin\" BINARY\n"
        "  TRACK 04 MODE2/2352\n"
        "    INDEX 01 00:00:00\n";

    const uint64_t size1 = 37032240;  // 15745 frames
    const uint64_t size2 = 33668880;  // 14315 frames
    const uint64_t size3 = 37020480;  // 15740 frames

    CUEParser p(cue);
    const CUETrackInfo *t;

    t = p.next_track(0);
    CHECK_EQ(t->session, 1, "t1 session");
    CHECK_EQ(t->track_start, 0, "t1 track_start");
    CHECK_EQ(strcmp(t->filename, "Track 01.bin"), 0, "t1 filename");

    t = p.next_track(size1);
    CHECK_EQ(t->session, 1, "t2 session");
    CHECK_EQ(t->file_start, 15745, "t2 file_start");
    CHECK_EQ(t->track_start, 15745, "t2 track_start");
    CHECK_EQ(t->data_start, 15745 + 117, "t2 data_start");  // INDEX 01 00:01:42
    CHECK_EQ(t->file_offset, 117ULL * 2352, "t2 file_offset");

    t = p.next_track(size2);
    CHECK_EQ(t->file_start, 15745 + 14315, "t3 file_start");
    CHECK_EQ(t->data_start, 15745 + 14315 + 165, "t3 data_start");  // INDEX 01 00:02:15

    t = p.next_track(size3);
    CHECK_EQ(t->session, 2, "t4 session");
    CHECK_EQ(t->track_mode, CUETrack_MODE2_2352, "t4 mode");
    CHECK_EQ(t->track_start, 15745 + 14315 + 15740, "t4 track_start");
    CHECK_EQ(t->file_offset, 0, "t4 file_offset");
}

// The normalized-cue contract: when CCueBinFileDevice rewrites a multi-bin
// session cue as a single-FILE cue with absolute INDEX times (session-2 LBAs
// shifted by the 11250-frame gap), the gadget-side CUEParser accounting must
// land every track's file_offset exactly where the device's virtual byte
// space (file bytes concatenated, gaps as zeros) puts its data.
static void test_normalized_cue_contract() {
    // Source: 3 audio bins (session 1) + 1 MODE2 bin (session 2),
    // INDEX 00 at file start for tracks 2+ (Redump style)
    const uint32_t frames1 = 15745, frames2 = 14315, frames3 = 15740;
    const uint32_t gap = 11250; // session 1 -> 2

    char normalized[1024];
    snprintf(normalized, sizeof(normalized),
             "FILE \"multibin\" BINARY\n"
             "REM SESSION 01\n"
             "  TRACK 01 AUDIO\n"
             "    INDEX 01 00:00:00\n"
             "  TRACK 02 AUDIO\n"
             "    INDEX 00 %02u:%02u:%02u\n"
             "    INDEX 01 %02u:%02u:%02u\n"
             "  TRACK 03 AUDIO\n"
             "    INDEX 00 %02u:%02u:%02u\n"
             "    INDEX 01 %02u:%02u:%02u\n"
             "REM SESSION 02\n"
             "  TRACK 04 MODE2/2352\n"
             "    INDEX 01 %02u:%02u:%02u\n",
             frames1 / 4500, (frames1 / 75) % 60, frames1 % 75,
             (frames1 + 117) / 4500, ((frames1 + 117) / 75) % 60, (frames1 + 117) % 75,
             (frames1 + frames2) / 4500, ((frames1 + frames2) / 75) % 60, (frames1 + frames2) % 75,
             (frames1 + frames2 + 165) / 4500, ((frames1 + frames2 + 165) / 75) % 60,
             (frames1 + frames2 + 165) % 75,
             (frames1 + frames2 + frames3 + gap) / 4500,
             ((frames1 + frames2 + frames3 + gap) / 75) % 60,
             (frames1 + frames2 + frames3 + gap) % 75);

    CUEParser p(normalized);
    const CUETrackInfo *t;

    t = p.next_track();
    CHECK_EQ(t->file_offset, 0, "n-t1 file_offset");

    t = p.next_track();
    // Track 2 data sits 117 frames into the second file
    CHECK_EQ(t->file_offset, ((uint64_t)frames1 + 117) * 2352, "n-t2 file_offset");
    CHECK_EQ(CUEByteOffset(*t, frames1), (uint64_t)frames1 * 2352, "n-t2 pregap maps to file 2 start");

    t = p.next_track();
    CHECK_EQ(t->file_offset, ((uint64_t)frames1 + frames2 + 165) * 2352, "n-t3 file_offset");

    t = p.next_track();
    CHECK_EQ(t->session, 2, "n-t4 session");
    // Track 4 data sits after all three files plus the 11250-frame gap zone
    CHECK_EQ(t->file_offset, ((uint64_t)frames1 + frames2 + frames3 + gap) * 2352, "n-t4 file_offset");
    // The device's segment for file 4 starts right after files 1-3 + gap,
    // which is exactly where CUEByteOffset() points for its first LBA
    CHECK_EQ(CUEByteOffset(*t, t->data_start), ((uint64_t)frames1 + frames2 + frames3 + gap) * 2352,
             "n-t4 data start maps to file 4 start");
}

static void test_cue_byte_offset() {
    CUETrackInfo t = {};
    t.sector_length = 2352;
    t.track_start = 1000;
    t.data_start = 1150;
    t.file_offset = 1150ULL * 2352;

    CHECK_EQ(CUEByteOffset(t, 1150), 1150ULL * 2352, "at data_start");
    CHECK_EQ(CUEByteOffset(t, 1200), 1200ULL * 2352, "in data");
    CHECK_EQ(CUEByteOffset(t, 1000), 1000ULL * 2352, "in stored pregap");
}

int main() {
    test_pregap_style_mixed_modes();
    test_index00_chain_single_file();
    test_multifile_pregap();
    test_redump_multibin_sessions();
    test_normalized_cue_contract();
    test_cue_byte_offset();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all cueparser tests passed\n");
    return 0;
}
