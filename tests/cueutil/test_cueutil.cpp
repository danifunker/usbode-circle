// Host-compiled unit tests for CueLBAToByteOffset / CueFindTrackForLBA
// (addon/cueparser/cueutil.cpp). Build and run with `make test` here;
// no Circle or cross-toolchain dependency.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include <cueparser/cueutil.h>

// Mirrors tests/mixedmode-repro/mixedmode-repro.cue: a 3689-sector
// MODE1/2048 data track followed by five 450-sector audio tracks stored
// contiguously in one BIN.
static const char *mixed_2048_cue =
    "FILE \"mixedmode-repro.bin\" BINARY\r\n"
    "  TRACK 01 MODE1/2048\r\n"
    "    INDEX 01 00:00:00\r\n"
    "  TRACK 02 AUDIO\r\n"
    "    INDEX 01 00:49:14\r\n"  // LBA 3689
    "  TRACK 03 AUDIO\r\n"
    "    INDEX 01 00:55:14\r\n"  // LBA 4139
    "  TRACK 04 AUDIO\r\n"
    "    INDEX 01 01:01:14\r\n"  // LBA 4589
    "  TRACK 05 AUDIO\r\n"
    "    INDEX 01 01:07:14\r\n"  // LBA 5039
    "  TRACK 06 AUDIO\r\n"
    "    INDEX 01 01:13:14\r\n"; // LBA 5489

// Same layout but with a raw MODE1/2352 data track: flat lba*2352 math must
// be preserved exactly (the common case that always worked).
static const char *mixed_2352_cue =
    "FILE \"raw.bin\" BINARY\r\n"
    "  TRACK 01 MODE1/2352\r\n"
    "    INDEX 01 00:00:00\r\n"
    "  TRACK 02 AUDIO\r\n"
    "    INDEX 01 00:49:14\r\n";

static const char *pure_audio_cue =
    "FILE \"audio.bin\" BINARY\r\n"
    "  TRACK 01 AUDIO\r\n"
    "    INDEX 01 00:00:00\r\n"
    "  TRACK 02 AUDIO\r\n"
    "    INDEX 01 00:06:00\r\n"; // LBA 450

static void test_mixed_2048() {
    const uint64_t data_bytes = 3689ULL * 2048;

    // Data track: cooked 2048-byte sectors.
    assert(CueLBAToByteOffset(mixed_2048_cue, 0) == 0);
    assert(CueLBAToByteOffset(mixed_2048_cue, 16) == 16ULL * 2048);
    assert(CueLBAToByteOffset(mixed_2048_cue, 3688) == 3688ULL * 2048);

    // Audio track starts (ground truth from concatenated BIN layout).
    assert(CueLBAToByteOffset(mixed_2048_cue, 3689) == data_bytes);
    assert(CueLBAToByteOffset(mixed_2048_cue, 4139) == data_bytes + 450ULL * 2352);
    assert(CueLBAToByteOffset(mixed_2048_cue, 4589) == data_bytes + 900ULL * 2352);
    assert(CueLBAToByteOffset(mixed_2048_cue, 5039) == data_bytes + 1350ULL * 2352);
    assert(CueLBAToByteOffset(mixed_2048_cue, 5489) == data_bytes + 1800ULL * 2352);

    // Mid-track LBA inside track 2.
    assert(CueLBAToByteOffset(mixed_2048_cue, 3689 + 100) == data_bytes + 100ULL * 2352);

    CUETrackInfo t;
    assert(CueFindTrackForLBA(mixed_2048_cue, 0, &t) && t.track_number == 1);
    assert(CueFindTrackForLBA(mixed_2048_cue, 3689, &t) && t.track_number == 2);
    assert(CueFindTrackForLBA(mixed_2048_cue, 5488, &t) && t.track_number == 5);
    assert(CueFindTrackForLBA(mixed_2048_cue, 99999, &t) && t.track_number == 6);
}

static void test_mixed_2352_matches_flat() {
    for (uint32_t lba : {0u, 16u, 3688u, 3689u, 4000u}) {
        assert(CueLBAToByteOffset(mixed_2352_cue, lba) == (uint64_t)lba * 2352);
    }
}

static void test_pure_audio_matches_flat() {
    for (uint32_t lba : {0u, 449u, 450u, 800u}) {
        assert(CueLBAToByteOffset(pure_audio_cue, lba) == (uint64_t)lba * 2352);
    }
}

static void test_fallbacks() {
    // Null or trackless cue sheets fall back to flat 2352 math.
    assert(CueLBAToByteOffset(nullptr, 100) == 100ULL * 2352);
    assert(CueLBAToByteOffset("", 100) == 100ULL * 2352);

    CUETrackInfo t;
    assert(!CueFindTrackForLBA(nullptr, 0, &t));
    assert(!CueFindTrackForLBA("", 0, &t));
}

static void test_stored_pregap_no_drift() {
    // Modeled on a real Alien Trilogy (USA) rip: raw 2352 data track, track 2
    // has a stored INDEX 00/01 pregap, tracks 3+ have INDEX 01 only. The
    // parser used to double-count the stored pregap when advancing to track 3,
    // shifting every later track 150 sectors (2 seconds) late.
    static const char *cue =
        "FILE \"usa.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 11:15:25\r\n"  // LBA 50650
        "    INDEX 01 11:17:25\r\n"  // LBA 50800
        "  TRACK 03 AUDIO\r\n"
        "    INDEX 01 13:27:55\r\n"; // LBA 60580

    // Uniform 2352 layout: correct offset is simply data_start * 2352.
    assert(CueLBAToByteOffset(cue, 50800) == 50800ULL * 2352);
    assert(CueLBAToByteOffset(cue, 60580) == 60580ULL * 2352);

    // Pure-audio disc where every track has a stored INDEX 00/01 pair; the
    // old parser drifted one pregap further per track.
    static const char *audio_cue =
        "FILE \"audio.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 00:06:00\r\n"  // LBA 450
        "    INDEX 01 00:08:00\r\n"  // LBA 600
        "  TRACK 03 AUDIO\r\n"
        "    INDEX 00 00:14:00\r\n"  // LBA 1050
        "    INDEX 01 00:16:00\r\n"; // LBA 1200
    assert(CueLBAToByteOffset(audio_cue, 600) == 600ULL * 2352);
    assert(CueLBAToByteOffset(audio_cue, 1200) == 1200ULL * 2352);
}

static void test_unstored_pregap() {
    // PREGAP frames exist on the disc (LBA space) but not in the file.
    static const char *cue =
        "FILE \"test.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2048\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    PREGAP 00:02:00\r\n"
        "    INDEX 01 00:53:25\r\n"  // file time 4000 -> disc data_start 4150
        "  TRACK 03 AUDIO\r\n"
        "    PREGAP 00:02:00\r\n"
        "    INDEX 01 00:59:25\r\n"; // file time 4450 -> disc data_start 4750

    const uint64_t data_bytes = 4000ULL * 2048;
    assert(CueLBAToByteOffset(cue, 4150) == data_bytes);
    assert(CueLBAToByteOffset(cue, 4750) == data_bytes + 450ULL * 2352);

    CUETrackInfo t;
    assert(CueFindTrackForLBA(cue, 4150, &t) && t.track_number == 2);
    assert(t.data_start == 4150 && t.track_start == 4000);
}

static void test_mds_synthesized_cue_toc() {
    // Shape of the cue CMDSFileDevice synthesizes (after the PREGAP-emission
    // fix): INDEX 01 times are absolute disc LBAs and no PREGAP lines are
    // written, so data_start must equal the INDEX 01 time exactly. Values
    // from a real Descent II MDS (track 2 at 38:17:22 = LBA 172297); the
    // old PREGAP-emitting synthesis shifted track 1 to LBA 150, which broke
    // ISO9660 detection in DOS.
    static const char *cue =
        "FILE \"DESCENT_II.mdf\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 38:17:22\n";
    CUETrackInfo t;
    assert(CueFindTrackForLBA(cue, 16, &t) && t.track_number == 1 && t.data_start == 0);
    assert(CueFindTrackForLBA(cue, 172297, &t) && t.track_number == 2 && t.data_start == 172297);
}

static void test_default_iso_cue() {
    // The built-in default cue used for plain ISO images must behave like
    // flat 2048 math (identical to the pre-fix block_size * lba behavior).
    static const char *iso_cue =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";
    assert(CueLBAToByteOffset(iso_cue, 0) == 0);
    assert(CueLBAToByteOffset(iso_cue, 16) == 16ULL * 2048);
}

int main() {
    test_mixed_2048();
    test_mixed_2352_matches_flat();
    test_pure_audio_matches_flat();
    test_stored_pregap_no_drift();
    test_unstored_pregap();
    test_mds_synthesized_cue_toc();
    test_fallbacks();
    test_default_iso_cue();

    printf("All cueutil tests passed.\n");
    return 0;
}
